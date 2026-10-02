#pragma once

class QWidget;

namespace ui::gurobi {

    // ============================================================================
    // Gurobi guides
    //
    // Step-by-step walkthroughs, one slide per screenshot, for installing
    // Gurobi and for getting its free academic license:
    //
    //   ┌──────────────────────────────────────────────────────────┐
    //   │  (badge)  Cómo instalar Gurobi                           │
    //   │           Paso 8 de 10 · captura 2 de 4                  │
    //   │           ┌ ⚠ Requirement (bold, amber) ───────────────┐ │
    //   │           (8) Step text, with its links                  │
    //   │           ┌──────────── screenshot ─────────────────┐   │
    //   │           │        (click to view it full screen)   │   │
    //   │           └──────────────────────────────────────────┘   │
    //   │                   Caption of this screenshot             │
    //   │                    ● ● ● ● ● ● ● ━ ○ ○                   │
    //   │                  [Cerrar] [‹ Anterior] [Siguiente ›]     │
    //   └──────────────────────────────────────────────────────────┘
    //
    // Siguiente / Anterior (or the arrow keys) go through every screenshot;
    // the dots jump straight to a step. The screenshots are embedded from
    // app/ui/resources/gurobi/<instalar|licencia>/NN.png, numbered in the
    // order they appear in the guide; a missing one shows a placeholder.
    // ============================================================================

    enum class Guide { Install, License };

    void showGuide(Guide guide, QWidget* parent);

} // namespace ui::gurobi
