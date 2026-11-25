
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CryptoFinanceSubsystem.h"
#include "ACryptoTerminalActor.generated.h"

class ACryptoSlotActor;
class UTexture2D;

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
 *
 * Se declara fuera de la clase para mantener el tipo de delegado
 * en el ámbito global y facilitar su procesamiento por UHT.
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
 * - Asignar las texturas de cada criptomoneda.
 * - Actualizar los slots cuando el Subsystem emite datos nuevos.
 * - Administrar el desplazamiento entre páginas.
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

    /**
     * Intervalo entre impresiones de información de depuración.
     * No afecta a la frecuencia de actualización financiera.
     */
    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Vantage|Debug",
        meta = (ClampMin = "0.1")
    )
    float PrintInterval = 2.0f;

    /** Activa o desactiva los mensajes de depuración en pantalla. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vantage|Debug")
    bool bEnableDebugOutput = true;

    // ------------------------------------------------------------
    // CONFIGURACIÓN DE VISUALIZACIÓN
    // ------------------------------------------------------------

    /** Modo espacial utilizado para distribuir los slots. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vantage|Layout")
    ETerminalMode CurrentLayoutMode = ETerminalMode::Mode3D_Horizontal;

    /**
     * Cantidad de slots visibles simultáneamente.
     * El valor predeterminado conserva el comportamiento anterior.
     */
    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Vantage|Layout",
        meta = (ClampMin = "1", ClampMax = "25")
    )
    int32 DisplaySlotCount = 5;

    /** Separación entre slots en los modos lineales. */
    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Vantage|Layout",
        meta = (ClampMin = "1.0")
    )
    float SlotSpacing = 120.0f;

    /** Radio utilizado para distribuir slots en modo circular. */
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

    /** Clase Blueprint o C++ utilizada para crear cada slot. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vantage|Setup")
    TSubclassOf<ACryptoSlotActor> BaseSlotClass;

    /**
     * Relaciona símbolos de mercado con sus texturas.
     *
     * Las claves pueden escribirse en mayúsculas o minúsculas.
     * El terminal normaliza los símbolos durante la búsqueda.
     *
     * También admite símbolos de mercado terminados en USDT.
     * Por ejemplo, BTCUSDT puede utilizar la clave BTC.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vantage|Setup")
    TMap<FString, TObjectPtr<UTexture2D>> CoinTextureMap;

    /** Permite desactivar la creación de actores desde el editor. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vantage|Setup")
    bool bSpawnSlotActors = true;

    // ------------------------------------------------------------
    // EVENTOS PÚBLICOS
    // ------------------------------------------------------------

    /** Evento que notifica qué criptomoneda fue seleccionada. */
    UPROPERTY(BlueprintAssignable, Category = "Vantage|Events")
    FOnCryptoAssetSelected OnCryptoAssetSelected;

    // ------------------------------------------------------------
    // PAGINACIÓN
    // ------------------------------------------------------------

    /**
     * Establece el desplazamiento global de la lista.
     * El límite superior se valida al actualizar los slots.
     */
    UFUNCTION(BlueprintCallable, Category = "Vantage|Terminal")
    static void SetGlobalScrollOffset(int32 NewOffset);

    /** Devuelve el desplazamiento global actual. */
    UFUNCTION(BlueprintPure, Category = "Vantage|Terminal")
    static int32 GetGlobalScrollOffset();

    /** Fuerza una actualización de los slots visibles. */
    UFUNCTION(BlueprintCallable, Category = "Vantage|Terminal")
    void RefreshTerminal();

    // ------------------------------------------------------------
    // DISTRIBUCIÓN ESPACIAL
    // ------------------------------------------------------------

    /**
     * Calcula la posición relativa de un slot.
     *
     * @param Index Índice del slot dentro de la página visible.
     * @param TotalElements Cantidad total de slots de la página.
     */
    UFUNCTION(BlueprintCallable, Category = "Vantage|Layout")
    FVector CalculateSlotPosition(int32 Index, int32 TotalElements) const;

    // ------------------------------------------------------------
    // INTERACCIÓN
    // ------------------------------------------------------------

    /** Procesa la selección de un activo por su símbolo. */
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

    /**
     * Recibe el evento de datos nuevos emitido por el Subsystem.
     *
     * Se ejecuta en Game Thread porque el Subsystem distribuye
     * sus actualizaciones desde su ticker.
     */
    UFUNCTION()
    void HandleCryptoDataUpdated(
        const TArray<FCryptoAssetData>& UpdatedAssets
    );

public:

    // ------------------------------------------------------------
    // ACTUALIZACIÓN POR FRAME
    // ------------------------------------------------------------

    /**
     * Tick utilizado para detectar cambios en la paginación
     * y, opcionalmente, mostrar información de depuración.
     */
    virtual void Tick(float DeltaTime) override;

private:

    // ------------------------------------------------------------
    // REFERENCIAS A SISTEMAS Y ACTORES
    // ------------------------------------------------------------

    /** Subsystem financiero utilizado por este terminal. */
    UPROPERTY(Transient)
    TObjectPtr<UCryptoFinanceSubsystem> FinanceSubsystem;

    /** Actores que representan los slots actualmente visibles. */
    UPROPERTY(Transient)
    TArray<TObjectPtr<ACryptoSlotActor>> SpawnedSlots;

    // ------------------------------------------------------------
    // ESTADO INTERNO
    // ------------------------------------------------------------

    /** Tiempo acumulado desde la última impresión de depuración. */
    float TimeSinceLastPrint = 0.0f;

    /** Último desplazamiento aplicado a los slots. */
    int32 LastAppliedScrollOffset = INDEX_NONE;

    /** Desplazamiento compartido por las instancias del terminal. */
    static int32 GlobalScrollOffset;

    // ------------------------------------------------------------
    // ADMINISTRACIÓN DE SLOTS
    // ------------------------------------------------------------

    /** Crea la cantidad de slots configurada en DisplaySlotCount. */
    void CreateSlotActors();

    /** Actualiza los datos, iconos y posiciones de los slots existentes. */
    void UpdateVisibleSlots();

    /** Ajusta el desplazamiento para que no supere el catálogo disponible. */
    void ClampScrollOffset(int32 TotalAssets);

    /**
     * Busca la textura correspondiente a un símbolo.
     *
     * Admite símbolos individuales y símbolos de mercado terminados
     * en USDT, como BTCUSDT.
     */
    UTexture2D* FindIconTexture(const FString& AssetSymbol) const;

    /** Obtiene una referencia válida al Subsystem financiero. */
    UCryptoFinanceSubsystem* ResolveFinanceSubsystem();

    /**
     * Muestra en pantalla los datos de los activos visibles.
     *
     * Es const porque solamente consulta información y no modifica
     * el estado interno del terminal.
     */
    void PrintVisibleAssets() const;
};
