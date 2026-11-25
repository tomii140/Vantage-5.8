
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CryptoFinanceSubsystem.h"
#include "ACryptoSlotActor.generated.h"

class UStaticMeshComponent;
class UWidgetComponent;
class UMaterialInstanceDynamic;
class UTexture2D;

/**
 * ACryptoSlotActor
 *
 * Representa visualmente un activo individual dentro del terminal.
 *
 * RESPONSABILIDADES:
 * - Mantener los datos financieros asignados al slot.
 * - Recibir la textura correspondiente a la criptomoneda.
 * - Crear una instancia dinámica e independiente del material.
 * - Aplicar la textura al plano 3D del icono.
 * - Notificar a Blueprint cuando cambian los datos.
 *
 * El Actor no consulta Binance ni accede directamente al Subsystem.
 * Recibe los datos desde ACryptoTerminalActor.
 */
UCLASS(Blueprintable)
class VANTAGE_API ACryptoSlotActor : public AActor
{
    GENERATED_BODY()

public:

    // ------------------------------------------------------------
    // CONSTRUCTOR
    // ------------------------------------------------------------

    /** Crea la jerarquía de componentes visuales del slot. */
    ACryptoSlotActor();

    // ------------------------------------------------------------
    // INICIALIZACIÓN
    // ------------------------------------------------------------

    /**
     * Inicializa o actualiza el contenido del slot.
     *
     * @param AssetData Datos financieros del activo.
     * @param IconTexture Textura PNG importada como Texture2D.
     */
    UFUNCTION(BlueprintCallable, Category = "Vantage|Slot")
    virtual void InitSlot(
        const FCryptoAssetData& AssetData,
        UTexture2D* IconTexture
    );

    /** Devuelve una copia de los datos actuales del slot. */
    UFUNCTION(BlueprintPure, Category = "Vantage|Slot")
    FCryptoAssetData GetCurrentAssetData() const;

protected:

    // ------------------------------------------------------------
    // CICLO DE VIDA
    // ------------------------------------------------------------

    virtual void BeginPlay() override;

    // ------------------------------------------------------------
    // COMPONENTES VISUALES
    // ------------------------------------------------------------

    /** Malla principal que representa la tarjeta. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Vantage|Components")
    TObjectPtr<UStaticMeshComponent> CardMeshComponent;

    /** Plano 3D donde se representa el logo de la criptomoneda. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Vantage|Components")
    TObjectPtr<UStaticMeshComponent> IconPlaneComponent;

    /** Widget 3D encargado de mostrar los datos financieros. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Vantage|Components")
    TObjectPtr<UWidgetComponent> UIWidgetComponent;

    // ------------------------------------------------------------
    // CONFIGURACIÓN DEL MATERIAL
    // ------------------------------------------------------------

    /**
     * Nombre del parámetro Texture2D del material.
     *
     * IMPORTANTE:
     * Debe coincidir exactamente con el nombre del parámetro
     * Texture Sample Parameter2D del material.
     */
    UPROPERTY(
        EditDefaultsOnly,
        BlueprintReadOnly,
        Category = "Vantage|Materials"
    )
    FName TextureParameterName = FName("IconTexture");

    /**
     * Índice del material de IconPlaneComponent que recibirá
     * la instancia dinámica.
     */
    UPROPERTY(
        EditDefaultsOnly,
        BlueprintReadOnly,
        Category = "Vantage|Materials",
        meta = (ClampMin = "0")
    )
    int32 IconMaterialIndex = 0;

    // ------------------------------------------------------------
    // EVENTOS BLUEPRINT
    // ------------------------------------------------------------

    /**
     * Notifica a Blueprint que el slot recibió datos nuevos.
     *
     * Se utiliza para actualizar el Widget:
     * - Símbolo.
     * - Nombre.
     * - Precio.
     * - Variación porcentual.
     * - Otros valores financieros.
     */
    UFUNCTION(BlueprintImplementableEvent, Category = "Vantage|Slot|Events")
    void BP_OnSlotDataUpdated(const FCryptoAssetData& AssetData);

private:

    // ------------------------------------------------------------
    // ESTADO INTERNO
    // ------------------------------------------------------------

    /** Instancia dinámica independiente del material original. */
    UPROPERTY(Transient)
    TObjectPtr<UMaterialInstanceDynamic> DynamicIconMaterial;

    /** Datos financieros actuales del slot. */
    UPROPERTY(Transient)
    FCryptoAssetData CurrentAssetData;

    /** Textura recibida desde el terminal. */
    UPROPERTY(Transient)
    TObjectPtr<UTexture2D> CurrentIconTexture;

    /** Última textura aplicada correctamente al material dinámico. */
    UPROPERTY(Transient)
    TObjectPtr<UTexture2D> LastAppliedIconTexture;

    /** Último símbolo para el que se informó que faltaba una textura. */
    FString LastMissingTextureSymbol;

    /** Indica si el slot ya recibió una inicialización. */
    bool bHasInitializedData = false;

    // ------------------------------------------------------------
    // FUNCIONES INTERNAS
    // ------------------------------------------------------------

    /**
     * Crea la instancia dinámica del material.
     * No crea una nueva instancia si ya existe una válida.
     */
    void CreateDynamicIconMaterial();

    /**
     * Aplica CurrentIconTexture al parámetro configurado.
     * También registra información de diagnóstico.
     */
    void ApplyIconTexture();
};
