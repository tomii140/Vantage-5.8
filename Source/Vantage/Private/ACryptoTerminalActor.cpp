#include "ACryptoTerminalActor.h"
#include "ACryptoSlotActor.h"
#include "CryptoFinanceSubsystem.h"
#include "Engine/Engine.h"

int32 ACryptoTerminalActor::GlobalScrollOffset = 0;

ACryptoTerminalActor::ACryptoTerminalActor()
{
	PrimaryActorTick.bCanEverTick = true;
}

void ACryptoTerminalActor::BeginPlay()
{
	Super::BeginPlay();
	UE_LOG(LogTemp, Log, TEXT("[Vantage] Terminal de criptomonedas inicializado."));
}

void ACryptoTerminalActor::SetGlobalScrollOffset(int32 NewOffset)
{
	GlobalScrollOffset = FMath::Max(0, NewOffset);
}

int32 ACryptoTerminalActor::GetGlobalScrollOffset()
{
	return GlobalScrollOffset;
}

FVector ACryptoTerminalActor::CalculateSlotPosition(int32 Index, int32 TotalElements) const
{
	const float Spacing = 120.0f;
	FVector Offset = FVector::ZeroVector;

	switch (CurrentLayoutMode)
	{
	case ETerminalMode::Mode3D_Horizontal:
		Offset = FVector(0.0f, (Index - TotalElements / 2.0f) * Spacing, 0.0f);
		break;

	case ETerminalMode::Mode3D_Vertical:
		Offset = FVector(0.0f, 0.0f, (TotalElements / 2.0f - Index) * Spacing);
		break;

	case ETerminalMode::Mode3D_Diagonal:
		Offset = FVector(0.0f, (Index - TotalElements / 2.0f) * Spacing, (TotalElements / 2.0f - Index) * Spacing);
		break;

	case ETerminalMode::Mode3D_Circular:
	{
		const float Angle = (2.0f * PI * Index) / FMath::Max(TotalElements, 1);
		const float Radius = 200.0f;
		Offset = FVector(0.0f, FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius);
		break;
	}
	default:
		break;
	}
	return Offset;
}

void ACryptoTerminalActor::HandleSlotTap(const FString& AssetSymbol)
{
	if (AssetSymbol.IsEmpty()) return;

	if (UGameInstance* GameInstance = GetGameInstance())
	{
		UCryptoFinanceSubsystem* FinanceSubsystem = GameInstance->GetSubsystem<UCryptoFinanceSubsystem>();
		if (FinanceSubsystem)
		{
			TArray<FCryptoAssetData> AllAssets;
			FinanceSubsystem->GetAllAssets(AllAssets);

			for (const FCryptoAssetData& Data : AllAssets)
			{
				if (Data.Symbol.Equals(AssetSymbol, ESearchCase::IgnoreCase))
				{
					OnCryptoAssetSelected.Broadcast(AssetSymbol);
					UE_LOG(LogTemp, Log, TEXT("[Vantage] Tap registrado en activo: %s | Precio: $%.2f"), *AssetSymbol, Data.LastPrice);
					break;
				}
			}
		}
	}
}

void ACryptoTerminalActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	TimeSinceLastPrint += DeltaTime;
	if (TimeSinceLastPrint < PrintInterval)
	{
		return;
	}
	TimeSinceLastPrint = 0.0f;

	if (UGameInstance* GameInstance = GetGameInstance())
	{
		UCryptoFinanceSubsystem* FinanceSubsystem = GameInstance->GetSubsystem<UCryptoFinanceSubsystem>();
		if (FinanceSubsystem)
		{
			TArray<FCryptoAssetData> AllAssets;
			FinanceSubsystem->GetAllAssets(AllAssets);

			if (AllAssets.Num() > 0)
			{
				for (int32 i = 0; i < DisplayLimit; ++i)
				{
					int32 TargetIndex = GlobalScrollOffset + i;

					if (TargetIndex < AllAssets.Num())
					{
						const FCryptoAssetData& Data = AllAssets[TargetIndex];

						FString DebugMessage = FString::Printf(
							TEXT("=== [SLOT %d] ===\n"
								"Símbolo: %s | Precio: $%.2f | Vol: %.2f"),
							i,
							*Data.Symbol,
							Data.LastPrice,
							Data.Volume
						);

						if (GEngine)
						{
							GEngine->AddOnScreenDebugMessage(10 + i, PrintInterval + 0.1f, FColor::Cyan, DebugMessage);
						}
					}
				}
			}
		}
	}
}