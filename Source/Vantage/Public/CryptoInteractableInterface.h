#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "CryptoInteractableInterface.generated.h"

// Esto permite que la interfaz sea visible y utilizable desde Blueprints
UINTERFACE(MinimalAPI)
class UCryptoInteractableInterface : public UInterface
{
	GENERATED_BODY()
};

/**
 * Interfaz universal para elementos interactivos (Carrusel 3D y paneles)
 */
class VANTAGE_API ICryptoInteractableInterface
{
	GENERATED_BODY()

public:
	// Eventos ejecutables e implementables tanto en C++ como en Blueprints

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Vantage|Interaction")
	void OnHoverStart();

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Vantage|Interaction")
	void OnHoverEnd();

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Vantage|Interaction")
	void OnSelectAsset();
};