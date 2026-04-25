// Copyright soft-ue-expert. All Rights Reserved.

#include "Subsystem/SoftUEBridgeSubsystem.h"
#include "Server/BridgeServer.h"
#include "SoftUEBridgeModule.h"
#include "Tools/GetLogsTool.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

void USoftUEBridgeSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	UE_LOG(LogSoftUEBridge, Log, TEXT("SoftUEBridgeSubsystem initializing"));

	FBridgeLogCapture::Get().Start();
	Server = MakeUnique<FBridgeServer>();
	StartServer(ResolvePort());
}

void USoftUEBridgeSubsystem::Deinitialize()
{
	StopServer();
	Server.Reset();
	FBridgeLogCapture::Get().Stop();

	UE_LOG(LogSoftUEBridge, Log, TEXT("SoftUEBridgeSubsystem deinitialized"));
	Super::Deinitialize();
}

bool USoftUEBridgeSubsystem::StartServer(int32 Port)
{
	if (Server.IsValid() && Server->IsRunning())
	{
		return true;
	}

	static constexpr int32 MaxPortAttempts = 10;
	static constexpr int32 MaxValidPort = 65535;
	for (int32 i = 0; i < MaxPortAttempts; ++i)
	{
		const int32 TryPort = Port + i;
		if (TryPort > MaxValidPort)
		{
			break;
		}
		// Use a local candidate so a failed Start() is fully destroyed before the next attempt
		TUniquePtr<FBridgeServer> Candidate = MakeUnique<FBridgeServer>();
		if (Candidate->Start(TryPort, TEXT("127.0.0.1")))
		{
			Server = MoveTemp(Candidate);
			WriteInstanceRegistry(TryPort);
			UE_LOG(LogSoftUEBridge, Log, TEXT("Bridge server listening on http://127.0.0.1:%d/bridge"), TryPort);
			return true;
		}
		UE_LOG(LogSoftUEBridge, Warning, TEXT("Port %d unavailable"), TryPort);
		// Candidate destroyed here, releasing any partially-acquired resources
	}

	UE_LOG(LogSoftUEBridge, Error, TEXT("Failed to start bridge server: no free port in range [%d, %d]"), Port, Port + MaxPortAttempts - 1);
	return false;
}

void USoftUEBridgeSubsystem::StopServer()
{
	if (Server.IsValid())
	{
		Server->Stop();
		DeleteInstanceRegistry();
	}
}

bool USoftUEBridgeSubsystem::IsServerRunning() const
{
	return Server.IsValid() && Server->IsRunning();
}

int32 USoftUEBridgeSubsystem::GetServerPort() const
{
	return Server.IsValid() ? Server->GetPort() : 0;
}

void USoftUEBridgeSubsystem::RestartServer()
{
	StopServer();
	StartServer(ResolvePort());
}

int32 USoftUEBridgeSubsystem::ResolvePort() const
{
	// Allow override via environment variable
	FString EnvPort = FPlatformMisc::GetEnvironmentVariable(TEXT("SOFT_UE_BRIDGE_PORT"));
	if (!EnvPort.IsEmpty())
	{
		int32 Port = FCString::Atoi(*EnvPort);
		if (Port > 0) return Port;
	}
	return 8080;
}

void USoftUEBridgeSubsystem::WriteInstanceRegistry(int32 Port) const
{
	// Write to <ProjectDir>/.soft-ue-bridge/instance.json
	FString Dir = FPaths::Combine(FPaths::ProjectDir(), TEXT(".soft-ue-bridge"));
	UE_LOG(LogSoftUEBridge, Log, TEXT("WriteInstanceRegistry: target dir = %s"), *Dir);

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if (!PlatformFile.DirectoryExists(*Dir))
	{
		const bool bCreated = PlatformFile.CreateDirectoryTree(*Dir);
		UE_LOG(LogSoftUEBridge, Log, TEXT("WriteInstanceRegistry: created dir = %s"), bCreated ? TEXT("yes") : TEXT("FAILED"));
		if (!bCreated)
		{
			UE_LOG(LogSoftUEBridge, Error, TEXT("WriteInstanceRegistry: cannot create directory, instance.json will not be written"));
			return;
		}
	}

	TSharedPtr<FJsonObject> Info = MakeShareable(new FJsonObject);
	Info->SetNumberField(TEXT("port"), Port);
	Info->SetStringField(TEXT("host"), TEXT("127.0.0.1"));

	FString JsonStr;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonStr);
	FJsonSerializer::Serialize(Info.ToSharedRef(), Writer);

	FString FilePath = FPaths::Combine(Dir, TEXT("instance.json"));
	const bool bSaved = FFileHelper::SaveStringToFile(JsonStr, *FilePath);
	if (bSaved)
	{
		UE_LOG(LogSoftUEBridge, Log, TEXT("WriteInstanceRegistry: written to %s"), *FilePath);
	}
	else
	{
		UE_LOG(LogSoftUEBridge, Error, TEXT("WriteInstanceRegistry: failed to write %s"), *FilePath);
	}
}

void USoftUEBridgeSubsystem::DeleteInstanceRegistry() const
{
	FString FilePath = FPaths::Combine(FPaths::ProjectDir(), TEXT(".soft-ue-bridge"), TEXT("instance.json"));
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if (PlatformFile.FileExists(*FilePath))
	{
		PlatformFile.DeleteFile(*FilePath);
	}
}
