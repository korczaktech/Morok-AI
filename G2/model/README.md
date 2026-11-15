# G2 model composition

This directory contains the generation-level composition layer for G2. It is not a fifth profile and is not shared with G1 or G3.

- `include/morok/g2/model`: public-to-G2 contracts for the two-expert model, its aggregated parameters, training, generation and checkpoint interface.
- `src`: implementation of the G2 composition and persistence layer.
- `morok::g2::model`: namespace for the composite model.
- `morok::g2::engine`: separate namespace and library layer for the individual recurrent experts.

The composition layer may call the G2 expert engine. It must never include or link G1 implementation files. Model-specific behavior belongs in `G2/Mokosh`, `G2/Tsukuyomi`, `G2/Artemis` or `G2/Apollo`.
