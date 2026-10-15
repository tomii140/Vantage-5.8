#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CryptoFinanceSubsystem.h"
#include "ACryptoTerminalActor.generated.h"

UCLASS()
class VANTAGE_API ACryptoTerminalActor : public AActor
{
    GENERATED_BODY()

public:
    ACryptoTerminalActor();

    // Movidas a public para garantizar que el Details Panel las renderice sin fallos de reflexión
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vantage|Config")
    FString TargetSymbol = TEXT("BTCUSDT");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vantage|Config")
    float PrintInterval = 2.0f;

protected:
    virtual void BeginPlay() override;

public:
    virtual void Tick(float DeltaTime) override;

private:
    float TimeSinceLastPrint = 0.0f; // Variable interna pura, sin UPROPERTY
};