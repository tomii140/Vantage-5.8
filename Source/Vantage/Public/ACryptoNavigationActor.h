
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ACryptoNavigationActor.generated.h"

class ACryptoTerminalActor;
class USceneComponent;
class UWidgetComponent;

/**
 * ACryptoNavigationActor
 *
 * Actor independiente responsable de alojar WBP_CryptoNavigation.
 *
 * Este actor no debe adjuntarse a ACryptoTerminalActor.
 * Su transform de mundo es independiente y se configura desde el nivel.
 *
 * RESPONSABILIDADES:
 * - Alojar el WidgetComponent de navegación.
 * - Permitir configurar sus dimensiones desde Details.
 * - Permitir configurar posición, rotación y escala.
 * - Mantener una referencia directa al terminal.
 * - Recibir las notificaciones de cambios de paginación.
 *
 * La lógica interna del UserWidget existente no se modifica.
 */
UCLASS()
class VANTAGE_API ACryptoNavigationActor : public AActor
{
    GENERATED_BODY()

public:

    // ========================================================
    // CONSTRUCTOR
    // ========================================================

    ACryptoNavigationActor();


    // ========================================================
    // REFERENCIA AL TERMINAL
    // ========================================================

    /**
     * Terminal al que pertenece esta interfaz de navegación.
     *
     * Asignar la instancia de ACryptoTerminalActor desde el editor.
     * No se realizan búsquedas globales de actores.
     */
    UPROPERTY(
        EditInstanceOnly,
        BlueprintReadWrite,
        Category = "Vantage|Navigation"
    )
    TObjectPtr<ACryptoTerminalActor> TerminalReference;


    // ========================================================
    // COMPONENTE VISUAL
    // ========================================================

    /**
     * Componente que aloja WBP_CryptoNavigation.
     *
     * En Widget Class se debe asignar WBP_CryptoNavigation.
     */
    UPROPERTY(
        VisibleAnywhere,
        BlueprintReadOnly,
        Category = "Vantage|Navigation"
    )
    TObjectPtr<UWidgetComponent> NavigationWidgetComponent;


    // ========================================================
    // DIMENSIONES DEL WIDGET
    // ========================================================

    /**
     * Dimensiones de dibujo del widget.
     *
     * X = ancho.
     * Y = alto.
     *
     * Estos valores determinan el espacio de dibujo del WidgetComponent.
     * Se pueden modificar directamente desde Details.
     */
    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Vantage|Navigation|Appearance",
        meta = (
            ClampMin = "1.0",
            UIMin = "1.0",
            UIMax = "3000.0",
            ToolTip = "Dimensiones del widget. X representa el ancho e Y representa el alto."
            )
    )
    FVector2D NavigationDrawSize = FVector2D(800.0f, 200.0f);


    // ========================================================
    // TRANSFORMACIÓN LOCAL DEL WIDGET
    // ========================================================

    /** Posición local del panel respecto al Root de este Actor. */
    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Vantage|Navigation|Transform"
    )
    FVector NavigationRelativeLocation = FVector::ZeroVector;

    /** Rotación local del panel respecto al Root de este Actor. */
    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Vantage|Navigation|Transform"
    )
    FRotator NavigationRelativeRotation = FRotator::ZeroRotator;

    /** Escala local del panel respecto al Root de este Actor. */
    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Vantage|Navigation|Transform"
    )
    FVector NavigationRelativeScale = FVector::OneVector;


protected:

    // ========================================================
    // CICLO DE VIDA
    // ========================================================

    virtual void OnConstruction(const FTransform& Transform) override;

    virtual void BeginPlay() override;

    virtual void EndPlay(
        const EEndPlayReason::Type EndPlayReason
    ) override;


    /** Recibe los cambios de paginación emitidos por el terminal. */
    UFUNCTION()
    void HandleTerminalNavigationUpdated();


private:

    // ========================================================
    // FUNCIONES INTERNAS
    // ========================================================

    /**
     * Aplica las dimensiones y transformación configuradas
     * al WidgetComponent.
     */
    void ApplyNavigationWidgetSettings();

    /** Inicializa el widget y le entrega la referencia al terminal. */
    void InitializeNavigationWidget();

    /** Actualiza el contador y el estado visual del widget. */
    void RefreshNavigationWidget();
};
