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

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vantage|Config")
    float PrintInterval = 2.0f;

    // Métodos para controlar el carrusel (desplazamiento global)
    UFUNCTION(BlueprintCallable, Category = "Vantage|Terminal")
    static void SetGlobalScrollOffset(int32 NewOffset);

    UFUNCTION(BlueprintCallable, Category = "Vantage|Terminal")
    static int32 GetGlobalScrollOffset();

protected:
    virtual void BeginPlay() override;

public:
    virtual void Tick(float DeltaTime) override;

private:
    float TimeSinceLastPrint = 0.0f;
    static int32 GlobalScrollOffset;
    const int32 DisplayLimit = 5; // Cantidad fija de pantallas simultáneas
};