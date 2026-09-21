# NRFusion Universal Standalone — Plano

O plano executável está dividido por fase:

- [docs/standalone/README.md](standalone/README.md)

Regra estrutural global: código first-party handwritten novo/tocado <=300 linhas físicas por arquivo; o cutover exige zero violações first-party.

Cada fase define escopo, implementação, revisão, validação e gate. O arquivo da fase atual é a fonte de verdade; o histórico monolítico permanece no Git.
