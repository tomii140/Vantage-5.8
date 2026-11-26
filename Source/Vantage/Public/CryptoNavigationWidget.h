
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CryptoNavigationWidget.generated.h"

class ACryptoTerminalActor;
class UButton;
class UTextBlock;

/**
 * UCryptoNavigationWidget
 *
 * Clase base C++ del widget de navegación.
 *
 * RESPONSABILIDADES:
 * - Recibir una referencia directa al terminal.
 * - Gestionar los botones de navegación.
 * - Solicitar cambios de página.
 * - Actualizar el contador visual.
 */
UCLASS()
class VANTAGE_API UCryptoNavigationWidget : public UUserWidget
{
    GENERATED_BODY()

public:

    // ------------------------------------------------------------
    // REFERENCIA AL TERMINAL
    // ------------------------------------------------------------

    /** Recibe el terminal que administra esta navegación. */
    void SetTerminalReference(ACryptoTerminalActor* InTerminal);

    // ------------------------------------------------------------
    // ACTUALIZACIÓN PÚBLICA
    // ------------------------------------------------------------

    /**
     * Actualiza el contador y el estado de los botones.
     *
     * Puede ser llamada desde ACryptoTerminalActor después
     * de actualizar los slots o cambiar de página.
     */
    UFUNCTION(BlueprintCallable, Category = "Vantage|Navigation")
    void RefreshNavigation();

protected:

    // ------------------------------------------------------------
    // COMPONENTES VINCULADOS DESDE WBP_CryptoNavigation
    // ------------------------------------------------------------

    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UButton> Previous_btn;

    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UTextBlock> Page_txt;

    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UButton> Next_btn;

    // ------------------------------------------------------------
    // CICLO DE VIDA
    // ------------------------------------------------------------

    virtual void NativeConstruct() override;

    virtual void NativeDestruct() override;

private:

    // ------------------------------------------------------------
    // REFERENCIA AL TERMINAL
    // ------------------------------------------------------------

    UPROPERTY(Transient)
    TObjectPtr<ACryptoTerminalActor> TerminalReference;

    // ------------------------------------------------------------
    // EVENTOS DE LOS BOTONES
    // ------------------------------------------------------------

    UFUNCTION()
    void HandlePreviousClicked();

    UFUNCTION()
    void HandleNextClicked();

    // ------------------------------------------------------------
    // ACTUALIZACIÓN INTERNA
    // ------------------------------------------------------------

    /** Implementación interna de la actualización visual. */
    void RefreshNavigationState();
};
