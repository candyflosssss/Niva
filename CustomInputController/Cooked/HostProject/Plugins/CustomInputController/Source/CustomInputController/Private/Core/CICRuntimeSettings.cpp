#include "Core/CICRuntimeSettings.h"

const UCICRuntimeSettings* UCICRuntimeSettings::Get()
{
	return GetDefault<UCICRuntimeSettings>();
}

