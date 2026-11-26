
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CryptoFinanceSubsystem.h"
#include "ACryptoTerminalActor.generated.h"

class ACryptoSlotActor;
class UTexture2D;
class USceneComponent;
class UWidgetComponent;

/**
 * Modos disponibles para distribuir espacialmente los slots.
 */
UENUM(BlueprintType)
enum class ETerminalMode : uint8
{
    Mode3D_Horizontal UMETA(DisplayName = "3D Horizontal"),
    Mode3D_Vertical   UMETA(DisplayName = "3D Vertical"),
    Mode3D_Diagonal   UMETA(DisplayName = "3D Diagonal"),
    Mode3D_Circular   UMETA(DisplayName = "3D Circular")
};

/**
 * Evento emitido cuando se selecciona un activo.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
    FOnCryptoAssetSelected,
    const FString&,
    AssetSymbol
);

/**
 * ACryptoTerminalActor
 *
 * Actor responsable de coordinar la presentación de los activos.
 *
 * RESPONSABILIDADES:
 * - Obtener los datos del CryptoFinanceSubsystem.
 * - Crear y administrar los slots visibles.
 * - Distribuir los slots según el modo espacial seleccionado.
 * - Resolver la KEY y la textura de cada criptomoneda.
 * - Actualizar los slots cuando el Subsystem emite datos nuevos.
 * - Administrar la paginación.
 * - Crear y administrar el componente de navegación global.
 *
 * No procesa mensajes JSON ni administra conexiones WebSocket.
 */
UCLASS()
class VANTAGE_API ACryptoTerminalActor : public AActor
{
    GENERATED_BODY()

public:

    // ------------------------------------------------------------
    // CONSTRUCTOR
    // ------------------------------------------------------------

    ACryptoTerminalActor();

    // ------------------------------------------------------------
    // CONFIGURACIÓN GENERAL
    // ------------------------------------------------------------

    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Vantage|Debug",
        meta = (ClampMin = "0.1")
    )
    float PrintInterval = 2.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vantage|Debug")
    bool bEnableDebugOutput = true;

    // ------------------------------------------------------------
    // CONFIGURACIÓN DE VISUALIZACIÓN
    // ------------------------------------------------------------

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vantage|Layout")
    ETerminalMode CurrentLayoutMode = ETerminalMode::Mode3D_Horizontal;

    /**
     * Cantidad de slots visibles simultáneamente.
     * También determina cuántos activos contiene cada página.
     */
    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Vantage|Layout",
        meta = (ClampMin = "1", ClampMax = "25")
    )
    int32 DisplaySlotCount = 5;

    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Vantage|Layout",
        meta = (ClampMin = "1.0")
    )
    float SlotSpacing = 120.0f;

    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Vantage|Layout",
        meta = (ClampMin = "1.0")
    )
    float CircularLayoutRadius = 200.0f;

    // ------------------------------------------------------------
    // CONFIGURACIÓN DE ACTORES
    // ------------------------------------------------------------

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vantage|Setup")
    TSubclassOf<ACryptoSlotActor> BaseSlotClass;

    /**
     * Relaciona las abreviaciones de las criptomonedas con sus texturas.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vantage|Setup")
    TMap<FString, TObjectPtr<UTexture2D>> CoinTextureMap;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vantage|Setup")
    bool bSpawnSlotActors = true;

    // ------------------------------------------------------------
    // NAVEGACIÓN GLOBAL
    // ------------------------------------------------------------

    /**
     * Componente visual único para la navegación del terminal.
     *
     * Su Widget Class se configura desde el editor con
     * WBP_CryptoNavigation.
     *
     * No pertenece a ningún ACryptoSlotActor.
     */
    UPROPERTY(
        VisibleAnywhere,
        BlueprintReadOnly,
        Category = "Vantage|Navigation"
    )
    TObjectPtr<UWidgetComponent> NavigationWidgetComponent;

    // ------------------------------------------------------------
    // EVENTOS PÚBLICOS
    // ------------------------------------------------------------

    UPROPERTY(BlueprintAssignable, Category = "Vantage|Events")
    FOnCryptoAssetSelected OnCryptoAssetSelected;

    // ------------------------------------------------------------
    // PAGINACIÓN
    // ------------------------------------------------------------

    UFUNCTION(BlueprintCallable, Category = "Vantage|Terminal")
    static void SetGlobalScrollOffset(int32 NewOffset);

    UFUNCTION(BlueprintPure, Category = "Vantage|Terminal")
    static int32 GetGlobalScrollOffset();

    UFUNCTION(BlueprintCallable, Category = "Vantage|Terminal|Pagination")
    void NavigatePage(int32 PageDelta);

    UFUNCTION(BlueprintPure, Category = "Vantage|Terminal|Pagination")
    int32 GetCurrentPageIndex() const;

    UFUNCTION(BlueprintCallable, Category = "Vantage|Terminal|Pagination")
    int32 GetTotalPageCount();

    UFUNCTION(BlueprintCallable, Category = "Vantage|Terminal")
    void RefreshTerminal();

    // ------------------------------------------------------------
    // DISTRIBUCIÓN ESPACIAL
    // ------------------------------------------------------------

    UFUNCTION(BlueprintCallable, Category = "Vantage|Layout")
    FVector CalculateSlotPosition(
        int32 Index,
        int32 TotalElements
    ) const;

    // ------------------------------------------------------------
    // INTERACCIÓN
    // ------------------------------------------------------------

    UFUNCTION(BlueprintCallable, Category = "Vantage|Input")
    void HandleSlotTap(const FString& AssetSymbol);

protected:

    // ------------------------------------------------------------
    // CICLO DE VIDA
    // ------------------------------------------------------------

    virtual void BeginPlay() override;

    virtual void EndPlay(
        const EEndPlayReason::Type EndPlayReason
    ) override;

    // ------------------------------------------------------------
    // EVENTOS DEL SUBSYSTEM
    // ------------------------------------------------------------

    UFUNCTION()
    void HandleCryptoDataUpdated(
        const TArray<FCryptoAssetData>& UpdatedAssets
    );

public:

    // ------------------------------------------------------------
    // ACTUALIZACIÓN POR FRAME
    // ------------------------------------------------------------

    virtual void Tick(float DeltaTime) override;

private:

    // ------------------------------------------------------------
    // REFERENCIAS A SISTEMAS Y ACTORES
    // ------------------------------------------------------------

    UPROPERTY(Transient)
    TObjectPtr<UCryptoFinanceSubsystem> FinanceSubsystem;

    UPROPERTY(Transient)
    TArray<TObjectPtr<ACryptoSlotActor>> SpawnedSlots;

    // ------------------------------------------------------------
    // ESTADO INTERNO
    // ------------------------------------------------------------

    float TimeSinceLastPrint = 0.0f;

    int32 LastAppliedScrollOffset = INDEX_NONE;

    static int32 GlobalScrollOffset;

    // ------------------------------------------------------------
    // ADMINISTRACIÓN DE SLOTS
    // ------------------------------------------------------------

    void CreateSlotActors();

    void UpdateVisibleSlots();

    void ClampScrollOffset(int32 TotalAssets);

    // ------------------------------------------------------------
    // RESOLUCIÓN DE PRESENTACIÓN
    // ------------------------------------------------------------

    bool ResolveAssetPresentation(
        const FString& AssetSymbol,
        FString& OutAssetKey,
        UTexture2D*& OutIconTexture
    ) const;

    // ------------------------------------------------------------
    // ADMINISTRACIÓN DE NAVEGACIÓN
    // ------------------------------------------------------------

    /**
     * Entrega la referencia de este terminal al widget de navegación
     * y actualiza su contador.
     */
    void InitializeNavigationWidget();

    /**
     * Actualiza el contador después de cambios en los slots
     * o en la paginación.
     */
    void RefreshNavigationWidget();

    // ------------------------------------------------------------
    // SUBSYSTEM Y DEPURACIÓN
    // ------------------------------------------------------------

    UCryptoFinanceSubsystem* ResolveFinanceSubsystem();

    void PrintVisibleAssets() const;
};
