
#include "CryptoNavigationWidget.h"

// Terminal.
#include "ACryptoTerminalActor.h"

// Componentes UMG.
#include "Components/Button.h"
#include "Components/TextBlock.h"


// ============================================================
// REFERENCIA AL TERMINAL
// ============================================================

void UCryptoNavigationWidget::SetTerminalReference(
    ACryptoTerminalActor* InTerminal
)
{
    TerminalReference = InTerminal;

    RefreshNavigationState();
}


// ============================================================
// CONSTRUCCIÓN DEL WIDGET
// ============================================================

void UCryptoNavigationWidget::NativeConstruct()
{
    Super::NativeConstruct();

    if (Previous_btn)
    {
        Previous_btn->OnClicked.AddUniqueDynamic(
            this,
            &UCryptoNavigationWidget::HandlePreviousClicked
        );
    }

    if (Next_btn)
    {
        Next_btn->OnClicked.AddUniqueDynamic(
            this,
            &UCryptoNavigationWidget::HandleNextClicked
        );
    }

    RefreshNavigationState();
}


// ============================================================
// DESTRUCCIÓN DEL WIDGET
// ============================================================

void UCryptoNavigationWidget::NativeDestruct()
{
    if (Previous_btn)
    {
        Previous_btn->OnClicked.RemoveDynamic(
            this,
            &UCryptoNavigationWidget::HandlePreviousClicked
        );
    }

    if (Next_btn)
    {
        Next_btn->OnClicked.RemoveDynamic(
            this,
            &UCryptoNavigationWidget::HandleNextClicked
        );
    }

    TerminalReference = nullptr;

    Super::NativeDestruct();
}


// ============================================================
// ACTUALIZACIÓN PÚBLICA
// ============================================================

/**
 * Punto de entrada público para actualizar la navegación.
 *
 * Lo utiliza ACryptoTerminalActor.
 */
void UCryptoNavigationWidget::RefreshNavigation()
{
    RefreshNavigationState();
}


// ============================================================
// ACTUALIZACIÓN INTERNA
// ============================================================

void UCryptoNavigationWidget::RefreshNavigationState()
{
    // --------------------------------------------------------
    // VERIFICAR REFERENCIA
    // --------------------------------------------------------

    if (!IsValid(TerminalReference))
    {
        if (Page_txt)
        {
            Page_txt->SetText(
                FText::FromString(TEXT("1 / 1"))
            );
        }

        if (Previous_btn)
        {
            Previous_btn->SetIsEnabled(false);
        }

        if (Next_btn)
        {
            Next_btn->SetIsEnabled(false);
        }

        return;
    }

    // --------------------------------------------------------
    // OBTENER ESTADO DEL TERMINAL
    // --------------------------------------------------------

    const int32 TotalPages = FMath::Max(
        1,
        TerminalReference->GetTotalPageCount()
    );

    const int32 CurrentPage = FMath::Clamp(
        TerminalReference->GetCurrentPageIndex() + 1,
        1,
        TotalPages
    );

    // --------------------------------------------------------
    // ACTUALIZAR CONTADOR
    // --------------------------------------------------------

    if (Page_txt)
    {
        const FText PageText = FText::Format(
            NSLOCTEXT(
                "VantageNavigation",
                "PageCounter",
                "{0} / {1}"
            ),
            FText::AsNumber(CurrentPage),
            FText::AsNumber(TotalPages)
        );

        Page_txt->SetText(PageText);
    }

    // --------------------------------------------------------
    // BOTÓN ANTERIOR
    // --------------------------------------------------------

    if (Previous_btn)
    {
        Previous_btn->SetIsEnabled(
            CurrentPage > 1
        );
    }

    // --------------------------------------------------------
    // BOTÓN SIGUIENTE
    // --------------------------------------------------------

    if (Next_btn)
    {
        Next_btn->SetIsEnabled(
            CurrentPage < TotalPages
        );
    }
}


// ============================================================
// EVENTO: PÁGINA ANTERIOR
// ============================================================

void UCryptoNavigationWidget::HandlePreviousClicked()
{
    if (!IsValid(TerminalReference))
    {
        return;
    }

    TerminalReference->NavigatePage(-1);

    RefreshNavigationState();
}


// ============================================================
// EVENTO: PÁGINA SIGUIENTE
// ============================================================

void UCryptoNavigationWidget::HandleNextClicked()
{
    if (!IsValid(TerminalReference))
    {
        return;
    }

    TerminalReference->NavigatePage(1);

    RefreshNavigationState();
}
