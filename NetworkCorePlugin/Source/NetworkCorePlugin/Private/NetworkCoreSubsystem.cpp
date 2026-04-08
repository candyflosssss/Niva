// Fill out your copyright notice in the Description page of Project Settings.

#include "NetworkCoreSubsystem.h"

void UNetworkCoreSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    HttpServerInstance = &FHttpServerModule::Get();
    Super::Initialize(Collection);
    Settings = GetDefault<UNivaNetworkCoreSettings>();

    const int32 Port = Settings->Port;
    FHttpServerModule& HttpServerModule = FHttpServerModule::Get();
    HttpRouter = HttpServerModule.GetHttpRouter(Port);

    if (!HttpRouter.IsValid())
    {
        UE_LOG(LogTemp, Error, TEXT("无法在端口 %d 初始化 IHttpRouter"), Port);
        return;
    }
}

void UNetworkCoreSubsystem::BindRoute(FString path, ENivaHttpRequestVerbs HttpVerbs, FNetworkCoreHttpServerDelegate OnHttpServerRequest)
{
    FHttpPath HttpPath(path);
    if (!HttpRouter)
    {
        return;
    }

    FHttpRouteHandle RouterHandle = HttpRouter->BindRoute(
        HttpPath,
        (EHttpServerRequestVerbs)HttpVerbs,
        FHttpRequestHandler::CreateLambda([this, OnHttpServerRequest](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
        {
            if (OnHttpServerRequest.IsBound())
            {
                FNivaHttpResponse HttpServerResponse = OnHttpServerRequest.Execute(FNivaHttpRequest(Request));
                TUniquePtr<FHttpServerResponse> Response = MakeUnique<FHttpServerResponse>();
                Response->Body = HttpServerResponse.HttpServerResponse.Body;
                Response->Code = HttpServerResponse.HttpServerResponse.Code;
                Response->Headers = HttpServerResponse.HttpServerResponse.Headers;
                Response->HttpVersion = HttpServerResponse.HttpServerResponse.HttpVersion;

                OnComplete(MoveTemp(Response));
                return true;
            }

            TUniquePtr<FHttpServerResponse> Response = FHttpServerResponse::Error(EHttpServerResponseCodes::NotFound);
            OnComplete(MoveTemp(Response));
            return true;
        })
    );

    CreatedRouteHandlers.Add(RouterHandle);
    HttpServerInstance->StartAllListeners();
}

void UNetworkCoreSubsystem::Deinitialize()
{
    if (HttpRouter.IsValid())
    {
        HttpServerInstance->StopAllListeners();
        for (FHttpRouteHandle HttpRouteHandle : CreatedRouteHandlers)
        {
            HttpRouter->UnbindRoute(HttpRouteHandle);
        }
        HttpRouter.Reset();
    }

    if (IsStarted)
    {
        IsStarted = false;
    }

    Super::Deinitialize();
}

void UNetworkCoreSubsystem::HandleHelloRequest(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
    TUniquePtr<FHttpServerResponse> Response = MakeUnique<FHttpServerResponse>();
    Response->Code = EHttpServerResponseCodes::Ok;
    Response->Headers.Add("Content-Type", { "text/html" });
    Response->Body = {
        0x3C, 0x68, 0x74, 0x6D, 0x6C, 0x3E, 0x3C, 0x68, 0x65, 0x61, 0x64,
        0x3E, 0x3C, 0x74, 0x69, 0x74, 0x6C, 0x65, 0x3E, 0x48, 0x65,
        0x6C, 0x6C, 0x6F, 0x20, 0x57, 0x6F, 0x72, 0x6C, 0x64, 0x3C,
    };

    OnComplete(MoveTemp(Response));
}

FNivaHttpResponse UNetworkCoreSubsystem::MakeResponse(FString Text, FString ContentType, int32 Code)
{
    FNivaHttpResponse HttpServerResponse;
    HttpServerResponse.HttpServerResponse.Code = (EHttpServerResponseCodes)Code;

    FTCHARToUTF8 ConvertToUtf8(*Text);
    const uint8* ConvertToUtf8Bytes = reinterpret_cast<const uint8*>(ConvertToUtf8.Get());
    HttpServerResponse.HttpServerResponse.Body.Append(ConvertToUtf8Bytes, ConvertToUtf8.Length());

    FString Utf8CharsetContentType = FString::Printf(TEXT("%s;charset=utf-8"), *ContentType);
    TArray<FString> ContentTypeValue = { MoveTemp(Utf8CharsetContentType) };
    HttpServerResponse.HttpServerResponse.Headers.Add(TEXT("content-type"), MoveTemp(ContentTypeValue));

    return HttpServerResponse;
}

FNivaHttpRequest::FNivaHttpRequest(const FHttpServerRequest& Request)
{
    Verb = (ENivaHttpRequestVerbs)Request.Verb;
    RelativePath = *Request.RelativePath.GetPath();

    for (const auto& Header : Request.Headers)
    {
        FString StrHeaderVals;
        for (const auto& Val : Header.Value)
        {
            StrHeaderVals += Val + TEXT(" ");
        }
        Headers.Add(Header.Key, StrHeaderVals);
    }

    PathParams = Request.PathParams;
    QueryParams = Request.QueryParams;

    FUTF8ToTCHAR BodyTCHARData(reinterpret_cast<const ANSICHAR*>(Request.Body.GetData()), Request.Body.Num());
    FString StrBodyData{ BodyTCHARData.Length(), BodyTCHARData.Get() };
    Body = *StrBodyData;
    BodyBytes = Request.Body;
}

