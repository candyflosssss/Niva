// Copyright RLoris 2024

#pragma once

#include "IPAddressAsyncResolve.h"
#include "Engine/LatentActionManager.h"
#include "Engine/World.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "LatentActions.h"
#include "SocketSubsystem.h"
#include "Templates/SharedPointer.h"
#include "SocketUtility.generated.h"

UENUM(BlueprintType)
enum class ESocketError : uint8
{
	None				UMETA(DisplayName = "None"),
	Invalid_Address		UMETA(DisplayName = "InvalidAddress"),
	Invalid_Socket		UMETA(DisplayName = "InvalidSocket"),
	Invalid_Context		UMETA(DisplayName = "InvalidContext"),
	Invalid_Thread		UMETA(DisplayName = "InvalidThread"),
	Bind_Error			UMETA(DisplayName = "BindError"),
	Connect_Error		UMETA(DisplayName = "ConnectError"),
	Open_Error			UMETA(DisplayName = "OpenError"),
	Close_Error			UMETA(DisplayName = "CloseError"),
	Listen_Error		UMETA(DisplayName = "ListenError"),
	Send_Error			UMETA(DisplayName = "SendError")
};

UENUM(BlueprintType)
enum class ESocketTextEncoding : uint8
{
	UTF_8		UMETA(DisplayName = "UTF-8"),
	ANSI		UMETA(DisplayName = "ANSI")
};

UENUM(BlueprintType)
enum class ESocketHelperIpProtocol : uint8
{
	Invalid		UMETA(DisplayName = "Invalid"),
	Ipv4		UMETA(DisplayName = "IPV4"),
	Ipv6		UMETA(DisplayName = "IPV6")
};

class FHostResolverAction : public FPendingLatentAction
{
public:
	FHostResolverAction(FString& HostIpRef, const FString& HostNameRef, bool& SuccessRef, const FLatentActionInfo& LatentInfo)
		: HostName(HostNameRef)
		, HostIp(HostIpRef)
		, Success(SuccessRef)
		, Resolver(nullptr)
		, ExecutionFunction(LatentInfo.ExecutionFunction)
		, OutputLink(LatentInfo.Linkage)
		, CallbackTarget(LatentInfo.CallbackTarget)
	{
		Success = false;
		TSharedPtr<FInternetAddr> ResolvedAddr = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateInternetAddr();
		if (!ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->GetHostByNameFromCache(TCHAR_TO_ANSI(*HostName), ResolvedAddr))
		{
			Resolver = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->GetHostByName(TCHAR_TO_ANSI(*HostName));
		}
		else
		{
			HostIp = ResolvedAddr->ToString(false);
			Success = true;
		}
	}

	virtual void UpdateOperation(FLatentResponse& Response) override
	{
		if (Resolver == nullptr)
		{
			Response.FinishAndTriggerIf(true, ExecutionFunction, OutputLink, CallbackTarget);
		}
		else if (Resolver->IsComplete())
		{
			Success = Resolver->GetErrorCode() == 0;
			if (Success)
			{
				HostIp = Resolver->GetResolvedAddress().ToString(false);
			}
			Response.FinishAndTriggerIf(true, ExecutionFunction, OutputLink, CallbackTarget);
		}
	}

protected:
	const FString& HostName;
	FString& HostIp;
	bool& Success;
	FResolveInfo* Resolver;
	FName ExecutionFunction;
	int32 OutputLink;
	FWeakObjectPtr CallbackTarget;
};

USTRUCT(BlueprintType)
struct FInfoAddr
{
	GENERATED_BODY();

	UPROPERTY(BlueprintReadOnly, Category = "SocketHelper|Utility")
	FString Ip;

	UPROPERTY(BlueprintReadOnly, Category = "SocketHelper|Utility")
	int32 Port;

	UPROPERTY(BlueprintReadOnly, Category = "SocketHelper|Utility")
	FString IpProtocol;

	UPROPERTY(BlueprintReadOnly, Category = "SocketHelper|Utility")
	FString SocketProtocol;
};

USTRUCT(BlueprintType)
struct FInfosAddr
{
	GENERATED_BODY();

	UPROPERTY(BlueprintReadOnly, Category = "SocketHelper|Utility")
	FString CanonicalName;

	UPROPERTY(BlueprintReadOnly, Category = "SocketHelper|Utility")
	TArray<FInfoAddr> Addresses;
};

USTRUCT(BlueprintType)
struct FSocketHelperAddress
{
	GENERATED_BODY();

	static inline const FString AnyIpv4 = TEXT("0.0.0.0");
	static inline const FString LoopbackIpv4 = TEXT("127.0.0.1");
	static inline const FString AnyIpv6 = TEXT("::");
	static inline const FString LoopbackIpv6 = TEXT("::1");

	FSocketHelperAddress() = default;
	explicit FSocketHelperAddress(TSharedRef<FInternetAddr> InAddress);
	explicit FSocketHelperAddress(const FString& InIp, const int32& InPort);
	explicit FSocketHelperAddress(const FString& InEndpoint);

	virtual ~FSocketHelperAddress();

	const FString& GetEndpoint() const { return Endpoint; }
	const FString& GetIp() const { return Ip; }
	const int32& GetPort() const { return Port; }
	const ESocketHelperIpProtocol& GetIpProtocol() const { return IpProtocol; }

	bool IsValidAddress() const
	{
		return bIsValidAddress;
	}

	bool IsEmptyAddress() const
	{
		return bIsEmptyAddress;
	}

	bool IsAnyAddress() const
	{
		return bIsAnyAddress;
	}

	bool IsLoopbackAddress() const
	{
		return bIsLoopbackAddress;
	}

	TSharedPtr<FInternetAddr> GetInternetAddress() const
	{
		if (bIsValidAddress && !Address.IsValid())
		{
			ConstCastSharedPtr<FInternetAddr>(Address) = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateInternetAddr();
			bool bIsIpValid;
			Address->SetIp(*Ip, bIsIpValid);
			Address->SetPort(Port);
		}
		return Address;
	}

	bool operator==(const FSocketHelperAddress& Other) const
	{
		return Endpoint == Other.Endpoint
			&& Ip == Other.Ip
			&& Port == Other.Port
			&& IpProtocol == Other.IpProtocol;
	}

	bool operator!=(const FSocketHelperAddress& Other) const
	{
		return Endpoint != Other.Endpoint
			|| Ip != Other.Ip
			|| Port != Other.Port
			|| IpProtocol != Other.IpProtocol;
	}

	friend uint32 GetTypeHash(const FSocketHelperAddress& InAddress)
	{
		return GetTypeHash(InAddress.GetEndpoint());
	}

protected:
	void Init();

	TSharedPtr<FInternetAddr> Address = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "SocketHelper|Utility")
	FString Endpoint;

	UPROPERTY(BlueprintReadOnly, Category = "SocketHelper|Utility")
	FString Ip;

	UPROPERTY(BlueprintReadOnly, Category = "SocketHelper|Utility")
	int32 Port = 0;

	UPROPERTY(BlueprintReadOnly, Category = "SocketHelper|Utility")
	ESocketHelperIpProtocol IpProtocol = ESocketHelperIpProtocol::Invalid;

	/** Is this a valid address */
	UPROPERTY(BlueprintReadOnly, Category = "SocketHelper|Utility")
	bool bIsValidAddress = false;

	/** Is this an empty address */
	UPROPERTY(BlueprintReadOnly, Category = "SocketHelper|Utility")
	bool bIsEmptyAddress = true;

	/** Is this the wildcard address */
	UPROPERTY(BlueprintReadOnly, Category = "SocketHelper|Utility")
	bool bIsAnyAddress = false;

	/** Is this the loopback address */
	UPROPERTY(BlueprintReadOnly, Category = "SocketHelper|Utility")
	bool bIsLoopbackAddress = false;
};

class FInfosAddrAction : public FPendingLatentAction
{
public:
	FInfosAddrAction(const FString& HostNameRef, const FString& ServiceRef, FInfosAddr& ResultRef, bool& SuccessRef, const FLatentActionInfo& LatentInfo)
		: HostName(HostNameRef)
		, Service(ServiceRef)
		, Result(ResultRef)
		, Done(false)
		, Success(SuccessRef)
		, ExecutionFunction(LatentInfo.ExecutionFunction)
		, OutputLink(LatentInfo.Linkage)
		, CallbackTarget(LatentInfo.CallbackTarget)
	{
		ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->GetAddressInfoAsync([this](FAddressInfoResult Infos)
		{
			Result.CanonicalName = Infos.CanonicalNameResult;
			for (auto Res : Infos.Results)
			{
				FInfoAddr Addr;
				Addr.Ip = Res.Address->ToString(!Service.IsEmpty());
				Addr.Port = Res.Address->GetPlatformPort();
				Addr.IpProtocol = Res.AddressProtocolName.ToString();
				Addr.SocketProtocol = Res.GetSocketTypeName().ToString();
				Result.Addresses.Add(Addr);
			}
			Success = (Infos.ReturnCode == 0);
			Done = true;
		},*HostName, Service.IsEmpty() ? nullptr : *Service, EAddressInfoFlags::Default | EAddressInfoFlags::FQDomainName, NAME_None, ESocketType::SOCKTYPE_Unknown);
	}

	virtual void UpdateOperation(FLatentResponse& Response) override
	{
		Response.FinishAndTriggerIf(Done, ExecutionFunction, OutputLink, CallbackTarget);
	}

protected:
	const FString& HostName;
	const FString& Service;
	FInfosAddr& Result;
	bool Done;
	bool& Success;
	FName ExecutionFunction;
	int32 OutputLink;
	FWeakObjectPtr CallbackTarget;
};

UCLASS()
class SOCKETHELPER_API USocketUtility : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Checks if the current platform supports socket */
	UFUNCTION(BlueprintPure, Category = "SocketHelper|Utility")
	static bool IsSocketSupported();

	/** Decode a byte array using a specific text encoding */
	UFUNCTION(BlueprintCallable, Category = "SocketHelper|Utility")
	static FString Decode(const TArray<uint8>& Bytes, ESocketTextEncoding Encoding);

	/** Encode a string using a specific text encoding */
	UFUNCTION(BlueprintCallable, Category = "SocketHelper|Utility")
	static TArray<uint8> Encode(const FString& Str, ESocketTextEncoding Encoding);

	/** Checks if an address is an IPV4 address */
	UFUNCTION(BlueprintPure, Category = "SocketHelper|Utility")
	static bool IsIpv4Address(const FString& Address);

	/** Checks if an address is an IPV6 address */
	UFUNCTION(BlueprintPure, Category = "SocketHelper|Utility")
	static bool IsIpv6Address(const FString& Address);

	/** Checks if this device has a network interface to communicate with other devices */
	UFUNCTION(BlueprintPure, Category = "SocketHelper|Utility")
	static bool HasNetworkInterface();

	/** Get the local network interfaces available on this device */
	UFUNCTION(BlueprintCallable, Category = "SocketHelper|Utility")
	static TArray<FString> GetLocalInterfaces();

	/** Try to get the device host name */
	UFUNCTION(BlueprintPure, Category = "SocketHelper|Utility")
	static bool GetMachineHostName(FString& HostName);

	/** Try to resolve an hostname and return an address */
	UFUNCTION(BlueprintCallable, Category = "SocketHelper|Utility", meta = (Latent, LatentInfo = "LatentInfo", HidePin = "WorldContextObject", DefaultToSelf = "WorldContextObject"))
	static void ResolveHostName(UObject* WorldContextObject, FLatentActionInfo LatentInfo, const FString& Host, bool& Success, FString& HostIp);

	UFUNCTION(BlueprintCallable, Category = "SocketHelper|Utility")
	static bool AddCachedHostName(const FString& HostName, const FString& HostIp);

	UFUNCTION(BlueprintCallable, Category = "SocketHelper|Utility")
	static bool RemoveCachedHostName(const FString& HostName);

	UFUNCTION(BlueprintCallable, Category = "SocketHelper|Utility", meta = (Latent, LatentInfo = "LatentInfo", HidePin = "WorldContextObject", DefaultToSelf = "WorldContextObject"))
	static void GetAddressInfos(UObject* WorldContextObject, FLatentActionInfo LatentInfo, const FString& HostName, const FString& Service, bool& Success, FInfosAddr& Result);

	/** Create an address from an ip and a port */
	UFUNCTION(BlueprintPure, Category = "SocketHelper|Utility")
	static FSocketHelperAddress MakeAddressFromIpPort(const FString& InIp, const int32& InPort);

	/** Create an address from an endpoint (ip+port) */
	UFUNCTION(BlueprintPure, Category = "SocketHelper|Utility")
	static FSocketHelperAddress MakeAddressFromEndpoint(const FString& InEndpoint);
};
