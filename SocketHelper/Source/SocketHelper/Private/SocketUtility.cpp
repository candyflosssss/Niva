// Copyright RLoris 2024

#include "SocketUtility.h"
#include "IPAddress.h"

FSocketHelperAddress::FSocketHelperAddress(const TSharedRef<FInternetAddr> InAddress)
{
	Ip = InAddress->ToString(false);
	Port = InAddress->GetPlatformPort();
	Address = InAddress;
	Endpoint = InAddress->ToString(true);

	Init();
}

FSocketHelperAddress::FSocketHelperAddress(const FString& InIp, const int32& InPort)
{
	Ip = InIp;
	Port = InPort;
	Address = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateInternetAddr();
	Address->SetIp(*Ip, bIsValidAddress);
	Address->SetPort(Port);
	Endpoint = Address->ToString(true);

	Init();
}

FSocketHelperAddress::FSocketHelperAddress(const FString& InEndpoint)
{
	Endpoint = InEndpoint;
	Address = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateInternetAddr();
	Address->SetIp(*Endpoint, bIsValidAddress);
	Ip = Address->ToString(false);
	Port = Address->GetPlatformPort();

	Init();
}

FSocketHelperAddress::~FSocketHelperAddress()
{
	Address.Reset();
}

void FSocketHelperAddress::Init()
{
	bIsValidAddress = Address->IsValid();
	bIsEmptyAddress = Endpoint.IsEmpty();

	if (Address->GetProtocolType() == FNetworkProtocolTypes::IPv4)
	{
		IpProtocol = ESocketHelperIpProtocol::Ipv4;

		if (Ip == AnyIpv4)
		{
			bIsValidAddress = true;
			bIsAnyAddress = true;
		}
		else if (Ip == LoopbackIpv4)
		{
			bIsValidAddress = true;
			bIsLoopbackAddress = true;
		}
	}
	else if (Address->GetProtocolType() == FNetworkProtocolTypes::IPv6)
	{
		IpProtocol = ESocketHelperIpProtocol::Ipv6;

		if (Ip == AnyIpv6)
		{
			bIsValidAddress = true;
			bIsAnyAddress = true;
		}
		else if (Ip == LoopbackIpv6)
		{
			bIsValidAddress = true;
			bIsLoopbackAddress = true;
		}
	}

	// Check port is valid
	if (bIsValidAddress && (Port < 0 || Port > 65535))
	{
		bIsValidAddress = false;
	}
}

bool USocketUtility::IsSocketSupported()
{
	return ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM) != nullptr;
}

FString USocketUtility::Decode(const TArray<uint8>& Bytes, ESocketTextEncoding Encoding)
{
	FString DecodeStr;

	switch (Encoding)
	{
	case ESocketTextEncoding::ANSI:
		DecodeStr = FString(ANSI_TO_TCHAR(reinterpret_cast<const char*>(Bytes.GetData())));
		break;
	case ESocketTextEncoding::UTF_8:
		DecodeStr = FString(UTF8_TO_TCHAR(reinterpret_cast<const char*>(Bytes.GetData())));
		break;
	}

	return DecodeStr;
}

TArray<uint8> USocketUtility::Encode(const FString& Str, ESocketTextEncoding Encoding)
{
	TArray<uint8> EncodeStr;
	auto Chars = Str.GetCharArray();

	switch (Encoding)
	{
	case ESocketTextEncoding::ANSI:
		EncodeStr.Append(reinterpret_cast<uint8*>(TCHAR_TO_ANSI(Chars.GetData())), Chars.Num());
		break;
	case ESocketTextEncoding::UTF_8:
		EncodeStr.Append(reinterpret_cast<uint8*>(TCHAR_TO_UTF8(Chars.GetData())), Chars.Num());
		break;
	}

	return EncodeStr;
}

bool USocketUtility::IsIpv4Address(const FString& Address)
{
	const TSharedRef<FInternetAddr> Addr = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateInternetAddr();
	bool IsValid = false;
	Addr->SetIp(*Address, IsValid);
	return IsValid && Addr->GetProtocolType() == FNetworkProtocolTypes::IPv4;
}

bool USocketUtility::IsIpv6Address(const FString& Address)
{
	const TSharedRef<FInternetAddr> Addr = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateInternetAddr();
	bool IsValid = false;
	Addr->SetIp(*Address, IsValid);
	return IsValid && Addr->GetProtocolType() == FNetworkProtocolTypes::IPv6;
}

bool USocketUtility::HasNetworkInterface()
{
	return ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->HasNetworkDevice();
}

TArray<FString> USocketUtility::GetLocalInterfaces()
{
	TArray<FString> Result;
	TArray<TSharedPtr<FInternetAddr>> Addresses;
	if (ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->GetLocalAdapterAddresses(Addresses))
	{
		for (const TSharedPtr<FInternetAddr>& Address : Addresses)
		{
			if (Address.IsValid())
			{
				Result.Add(Address->ToString(false));
			}
		}
	}
	return Result;
}

bool USocketUtility::GetMachineHostName(FString& HostName)
{
	ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->GetHostName(HostName);
	return !HostName.IsEmpty();
}

void USocketUtility::ResolveHostName(UObject* WorldContextObject, struct FLatentActionInfo LatentInfo, const FString& Host, bool& Success, FString& HostIp)
{
	Success = false;
	if (UWorld* World = WorldContextObject->GetWorld())
	{
		FLatentActionManager& LatentActionManager = World->GetLatentActionManager();
		if (LatentActionManager.FindExistingAction<FHostResolverAction>(LatentInfo.CallbackTarget, LatentInfo.UUID) == nullptr)
		{
			LatentActionManager.AddNewAction(LatentInfo.CallbackTarget, LatentInfo.UUID, new FHostResolverAction(HostIp, Host, Success, LatentInfo));
		}
	}
}

bool USocketUtility::AddCachedHostName(const FString& HostName, const FString& HostIp)
{
	const TSharedRef<FInternetAddr> Addr = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateInternetAddr();
	bool IsValid = false;
	Addr->SetIp(*HostIp, IsValid);
	if (IsValid)
	{
		ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->AddHostNameToCache(TCHAR_TO_ANSI(*HostName), Addr);
		return true;
	}
	return false;
}

bool USocketUtility::RemoveCachedHostName(const FString& HostName)
{
	ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->RemoveHostNameFromCache(TCHAR_TO_ANSI(*HostName));
	return true;
}

void USocketUtility::GetAddressInfos(UObject* WorldContextObject, struct FLatentActionInfo LatentInfo, const FString& HostName, const FString& Service, bool& Success, FInfosAddr& Result)
{
	Success = false;
	if (UWorld* World = WorldContextObject->GetWorld())
	{
		FLatentActionManager& LatentActionManager = World->GetLatentActionManager();
		if (LatentActionManager.FindExistingAction<FHostResolverAction>(LatentInfo.CallbackTarget, LatentInfo.UUID) == nullptr)
		{
			LatentActionManager.AddNewAction(LatentInfo.CallbackTarget, LatentInfo.UUID, new FInfosAddrAction(HostName, Service, Result, Success, LatentInfo));
		}
	}
}

FSocketHelperAddress USocketUtility::MakeAddressFromIpPort(const FString& InIp, const int32& InPort)
{
	return FSocketHelperAddress(InIp, InPort);
}

FSocketHelperAddress USocketUtility::MakeAddressFromEndpoint(const FString& InEndpoint)
{
	return FSocketHelperAddress(InEndpoint);
}