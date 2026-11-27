#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CryptoFinanceSubsystem.h"
#include "ACryptoTerminalActor.generated.h"

class ACryptoSlotActor;
class UTexture2D;
class USceneComponent;

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
 * Notifica qué activo financiero fue seleccionado desde un slot.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
    FOnCryptoAssetSelected,
    const FString&,
    AssetSymbol
);

/**
 * Notifica que el estado de paginación del terminal cambió.
 *
 * ACryptoNavigationActor escucha este evento para refrescar el contador
 * del widget. El terminal no necesita conocer ni administrar el widget.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnTerminalNavigationUpdated);

/**
 * Actor principal del terminal de criptomonedas Vantage.
 *
 * Responsabilidades:
 * - Obtener y distribuir datos del Subsystem financiero.
 * - Crear y administrar los actores visuales de slots.
 * - Resolver las KEY y texturas de los activos.
 * - Calcular la distribución espacial.
 * - Administrar la paginación.
 *
 * La interfaz de navegación vive en ACryptoNavigationActor, un Actor
 * independiente que se comunica mediante OnTerminalNavigationUpdated
 * y conserva su propia referencia a este terminal.
 *
 * Jerarquía de componentes:
 *
 * RootComponent
 * └── SlotsRootComponent
 *     └── SpawnedSlots
 */
UCLASS()
class VANTAGE_API ACryptoTerminalActor : public AActor
{
    GENERATED_BODY()

public:

    ACryptoTerminalActor();

    // ========================================================
    // CONFIGURACIÓN DE DEPURACIÓN
    // ========================================================

    /** Intervalo entre impresiones periódicas de diagnóstico. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vantage|Debug", meta = (ClampMin = "0.1"))
    float PrintInterval = 2.0f;

    /** Habilita o deshabilita los mensajes periódicos de depuración. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vantage|Debug")
    bool bEnableDebugOutput = true;


    // ========================================================
    // CONFIGURACIÓN DE DISTRIBUCIÓN DE SLOTS
    // ========================================================

    /** Modo espacial utilizado para distribuir las tarjetas. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vantage|Layout")
    ETerminalMode CurrentLayoutMode = ETerminalMode::Mode3D_Horizontal;

    /** Cantidad de slots visibles simultáneamente. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vantage|Layout", meta = (ClampMin = "1", ClampMax = "25"))
    int32 DisplaySlotCount = 5;

    /** Separación entre slots según el modo de distribución. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vantage|Layout", meta = (ClampMin = "1.0"))
    float SlotSpacing = 120.0f;

    /** Radio utilizado por la distribución circular. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vantage|Layout", meta = (ClampMin = "1.0"))
    float CircularLayoutRadius = 200.0f;


    // ========================================================
    // TRANSFORMACIÓN DEL GRUPO DE SLOTS
    // ========================================================

    /** Componente padre exclusivo de los actores visuales de slots. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Vantage|Components")
    TObjectPtr<USceneComponent> SlotsRootComponent;

    /** Posición local del conjunto completo de slots respecto al Root. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vantage|Layout|Transform")
    FVector SlotsRelativeLocation = FVector::ZeroVector;

    /** Rotación local del conjunto completo de slots respecto al Root. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vantage|Layout|Transform")
    FRotator SlotsRelativeRotation = FRotator::ZeroRotator;

    /** Escala local del conjunto completo de slots respecto al Root. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vantage|Layout|Transform")
    FVector SlotsRelativeScale = FVector::OneVector;


    // ========================================================
    // CONFIGURACIÓN DE ACTIVOS E ICONOS
    // ========================================================

    /** Clase nativa o Blueprint utilizada para instanciar cada slot. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vantage|Setup")
    TSubclassOf<ACryptoSlotActor> BaseSlotClass;

    /** Relación entre la KEY del activo y su textura de icono. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vantage|Setup")
    TMap<FString, TObjectPtr<UTexture2D>> CoinTextureMap;

    /** Permite activar o desactivar la creación automática de slots. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vantage|Setup")
    bool bSpawnSlotActors = true;


    // ========================================================
    // EVENTOS PÚBLICOS
    // ========================================================

    /** Evento emitido cuando el usuario selecciona un activo. */
    UPROPERTY(BlueprintAssignable, Category = "Vantage|Events")
    FOnCryptoAssetSelected OnCryptoAssetSelected;

    /**
     * Evento emitido cuando cambia el estado de paginación.
     *
     * ACryptoNavigationActor se suscribe para actualizar el contador
     * visual, sin que este terminal conozca el UserWidget.
     */
    UPROPERTY(BlueprintAssignable, Category = "Vantage|Events|Pagination")
    FOnTerminalNavigationUpdated OnTerminalNavigationUpdated;


    // ========================================================
    // API PÚBLICA: PAGINACIÓN Y ACTUALIZACIÓN
    // ========================================================

    /** Establece el desplazamiento global de la paginación. */
    UFUNCTION(BlueprintCallable, Category = "Vantage|Terminal")
    static void SetGlobalScrollOffset(int32 NewOffset);

    /** Devuelve el desplazamiento global actual. */
    UFUNCTION(BlueprintPure, Category = "Vantage|Terminal")
    static int32 GetGlobalScrollOffset();

    /** Navega una cantidad relativa de páginas. */
    UFUNCTION(BlueprintCallable, Category = "Vantage|Terminal|Pagination")
    void NavigatePage(int32 PageDelta);

    /** Devuelve el índice de página actual, comenzando en cero. */
    UFUNCTION(BlueprintPure, Category = "Vantage|Terminal|Pagination")
    int32 GetCurrentPageIndex() const;

    /** Devuelve la cantidad total de páginas disponibles. */
    UFUNCTION(BlueprintCallable, Category = "Vantage|Terminal|Pagination")
    int32 GetTotalPageCount();

    /** Solicita una actualización visual del terminal. */
    UFUNCTION(BlueprintCallable, Category = "Vantage|Terminal")
    void RefreshTerminal();

    /** Calcula la posición local de un slot según el layout activo. */
    UFUNCTION(BlueprintCallable, Category = "Vantage|Layout")
    FVector CalculateSlotPosition(int32 Index, int32 TotalElements) const;

    /** Procesa la selección de un activo desde un slot. */
    UFUNCTION(BlueprintCallable, Category = "Vantage|Input")
    void HandleSlotTap(const FString& AssetSymbol);


protected:

    // ========================================================
    // CICLO DE VIDA Y EVENTOS INTERNOS
    // ========================================================

    virtual void OnConstruction(const FTransform& Transform) override;
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    /** Callback del Subsystem financiero cuando cambian los datos. */
    UFUNCTION()
    void HandleCryptoDataUpdated(const TArray<FCryptoAssetData>& UpdatedAssets);


public:

    /** Tick utilizado para detectar cambios de página y depuración. */
    virtual void Tick(float DeltaTime) override;


private:

    // ========================================================
    // ESTADO INTERNO
    // ========================================================

    /** Subsystem financiero utilizado como fuente de datos. */
    UPROPERTY(Transient)
    TObjectPtr<UCryptoFinanceSubsystem> FinanceSubsystem;

    /** Actores de slot creados y administrados por este terminal. */
    UPROPERTY(Transient)
    TArray<TObjectPtr<ACryptoSlotActor>> SpawnedSlots;

    /** Tiempo acumulado para la salida periódica de diagnóstico. */
    float TimeSinceLastPrint = 0.0f;

    /** Último desplazamiento aplicado a los slots visibles. */
    int32 LastAppliedScrollOffset = INDEX_NONE;

    /** Desplazamiento compartido entre instancias del terminal. */
    static int32 GlobalScrollOffset;


    // ========================================================
    // FUNCIONES PRIVADAS DE GESTIÓN
    // ========================================================

    /** Crea y adjunta los actores de slot a SlotsRootComponent. */
    void CreateSlotActors();

    /** Actualiza los slots con los activos de la página actual. */
    void UpdateVisibleSlots();

    /** Ajusta el desplazamiento a los límites del catálogo. */
    void ClampScrollOffset(int32 TotalAssets);

    /**
     * Resuelve la KEY y textura del activo a partir de su símbolo.
     * Devuelve false si no existe una presentación configurada.
     */
    bool ResolveAssetPresentation(
        const FString& AssetSymbol,
        FString& OutAssetKey,
        UTexture2D*& OutIconTexture
    ) const;

    /** Aplica el transform editable al grupo de slots. */
    void ApplySlotsRootTransform();

    /** Obtiene la referencia al Subsystem financiero. */
    UCryptoFinanceSubsystem* ResolveFinanceSubsystem();

    /** Imprime información de los activos visibles para depuración. */
    void PrintVisibleAssets() const;
};
