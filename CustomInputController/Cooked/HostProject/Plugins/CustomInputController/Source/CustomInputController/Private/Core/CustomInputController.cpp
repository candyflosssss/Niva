// Copyright Epic Games, Inc. All Rights Reserved.

#include "Core/CustomInputController.h"

#include "IInputDeviceModule.h"
#include "Core/CustomInputKey.h"

#define LOCTEXT_NAMESPACE "FCustomInputControllerModule"

void FCustomInputControllerModule::StartupModule()
{
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module
	
	FMyCustomInputKeys::AddKeys();

	IModularFeatures::Get().RegisterModularFeature(
		IInputDeviceModule::GetModularFeatureName(), 
		this
	);
}

void FCustomInputControllerModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.
	IModularFeatures::Get().UnregisterModularFeature(
		IInputDeviceModule::GetModularFeatureName(), 
		this
	);
}

TSharedPtr<IInputDevice> FCustomInputControllerModule::CreateInputDevice(
	const TSharedRef<FGenericApplicationMessageHandler>& InMessageHandler)
{
	return MakeShareable(new FUDPInputDevice(InMessageHandler));
}


#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FCustomInputControllerModule, CustomInputController)
