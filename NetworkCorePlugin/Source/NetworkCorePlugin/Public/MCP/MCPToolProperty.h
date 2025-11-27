// Auto-split from NetworkCoreSubsystem.h — MCP tool property declarations
#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "MCP/MCPTypes.h"

// 前置声明，避免额外依赖并消除循环包含
class AActor;
class UActorComponent;
class UMcpExposableBaseComponent;

#include "MCPToolProperty.generated.h"

UCLASS(BlueprintType)
class NETWORKCOREPLUGIN_API UMCPToolProperty : public UObject
{
    GENERATED_BODY()

public:
    UPROPERTY(BlueprintReadWrite, Category = "NetworkCore|MCP|Tool")
    FString Name;

    UPROPERTY(BlueprintReadWrite, Category = "NetworkCore|MCP|Tool")
    EMCPJsonType Type;
    
    UPROPERTY(BlueprintReadWrite, Category = "NetworkCore|MCP|Tool")
    FString Description;

    UMCPToolProperty(FString InName, EMCPJsonType InType, FString InDescription)
        : Name(InName)
        , Type(InType)
        , Description(InDescription)
    {}
    
    UMCPToolProperty()
        : Name(TEXT(""))
        , Type(EMCPJsonType::None)
        , Description(TEXT(""))
    {}

    virtual TSharedPtr<FJsonObject> GetJsonObject()
    {
        return nullptr;
    }

    virtual TArray<FString> GetAvailableTargets()
    {
        return TArray<FString>();
    }
};

UCLASS(BlueprintType)
class NETWORKCOREPLUGIN_API UMCPToolPropertyString : public UMCPToolProperty
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable,BlueprintPure, Category = "NetworkCore|MCP|Tool")
    static UMCPToolProperty* CreateStringProperty(FString InName, FString InDescription);

    virtual TSharedPtr<FJsonObject> GetJsonObject() override;

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "NetworkCore|MCP|Tool")
    FString GetValue(FString InJson);
};

UCLASS(BlueprintType)
class NETWORKCOREPLUGIN_API UMCPToolPropertyNumber : public UMCPToolProperty
{
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, Category = "NetworkCore|MCP|Tool")
    int Min;
    
    UPROPERTY(BlueprintReadWrite, Category = "NetworkCore|MCP|Tool")
    int Max;

    UPROPERTY(BlueprintReadWrite, Category = "NetworkCore|MCP|Tool")
    int Default = 0;
    
    UFUNCTION(BlueprintCallable,BlueprintPure, Category = "NetworkCore|MCP|Tool")
    static UMCPToolProperty* CreateNumberProperty(FString InName, FString InDescription, int InMin, int InMax);

    virtual TSharedPtr<FJsonObject> GetJsonObject() override;

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "NetworkCore|MCP|Tool")
    float GetValue(FString InJson);
};

UCLASS(BlueprintType)
class NETWORKCOREPLUGIN_API UMCPToolPropertyInt : public UMCPToolProperty
{
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, Category = "NetworkCore|MCP|Tool")
    int Min;
    
    UPROPERTY(BlueprintReadWrite, Category = "NetworkCore|MCP|Tool")
    int Max;

    UPROPERTY(BlueprintReadWrite, Category = "NetworkCore|MCP|Tool")
    int Default = 0;
    
    UFUNCTION(BlueprintCallable,BlueprintPure, Category = "NetworkCore|MCP|Tool")
    static UMCPToolProperty* CreateIntProperty(FString InName, FString InDescription, int InMin, int InMax);

    virtual TSharedPtr<FJsonObject> GetJsonObject() override;

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "NetworkCore|MCP|Tool")
    int GetValue(FString InJson);
};

// 保留占位（已在前置声明处声明）

UCLASS(BlueprintType)
class NETWORKCOREPLUGIN_API UMCPToolPropertyActorPtr: public UMCPToolProperty
{
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadOnly, Category = "NetworkCore|MCP|Tool", meta=(MetaClass="Actor", AllowAbstract="false"))
    TSubclassOf<AActor> ActorClass;

    // 存储对象引用图表,用于快速查找，每次FindActors时，都需要重新构建图表
    TMap<FString, TWeakObjectPtr<AActor>> ActorMap;
    
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "NetworkCore|MCP|Tool")
    static UMCPToolProperty* CreateActorPtrProperty(FString InName, FString InDescription, TSubclassOf<AActor> InActorClass);

    virtual TSharedPtr<FJsonObject> GetJsonObject() override;
    
    virtual TArray<FString> GetAvailableTargets() override;

    UFUNCTION(BlueprintCallable)
    AActor* GetActor(FString InName);

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "NetworkCore|MCP|Tool")
    AActor* GetValue(FString InJson);

    // 根据静态类，查找场景中所有符合条件的对象
    UFUNCTION(BlueprintCallable, Category = "NetworkCore|MCP|Tool")
    TArray<AActor*> FindActors();
};

UCLASS(BlueprintType)
class NETWORKCOREPLUGIN_API UMCPToolPropertyComponentPtr: public UMCPToolProperty
{
    GENERATED_BODY()
public:
    // Target component base class (must derive from UMcpExposableBaseComponent)
    UPROPERTY(BlueprintReadOnly, Category = "NetworkCore|MCP|Tool", meta=(MetaClass="McpExposableBaseComponent", AllowAbstract="false"))
    TSubclassOf<class UMcpExposableBaseComponent> ComponentClass;

    // Map from readable name to component (weak)
    TMap<FString, TWeakObjectPtr<class UMcpExposableBaseComponent>> ComponentMap;

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "NetworkCore|MCP|Tool")
    static UMCPToolProperty* CreateComponentPtrProperty(FString InName, FString InDescription, TSubclassOf<class UMcpExposableBaseComponent> InComponentClass);

    virtual TSharedPtr<FJsonObject> GetJsonObject() override;

    virtual TArray<FString> GetAvailableTargets() override;

    UFUNCTION(BlueprintCallable)
    UActorComponent* GetComponentByLabel(const FString& InLabel);

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "NetworkCore|MCP|Tool")
    UActorComponent* GetValue(FString InJson);
};

UCLASS(BlueprintType)
class NETWORKCOREPLUGIN_API UMCPToolPropertyArray: public UMCPToolProperty
{
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadOnly, Category = "NetworkCore|MCP|Tool")
    UMCPToolProperty* Property;

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "NetworkCore|MCP|Tool")
    static UMCPToolProperty* CreateArrayProperty(FString InName, FString InDescription, UMCPToolProperty* InProperty);

    virtual TSharedPtr<FJsonObject> GetJsonObject() override;
};
