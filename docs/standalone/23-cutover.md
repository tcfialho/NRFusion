# Fase 23 — A/B final e cutover do OptiScaler

## Objetivo

Trocar host/distribuição somente após equivalência, performance, packaging e regra estrutural completas.

## Dependências

Fase 22.

## Fora de escopo

- Apagar fallback cedo
- Claim GPU sem hardware

## Implementação

- [ ] Comparar CPU p50/p95/p99, allocations, locks, creates, queries e copies.
- [ ] Comparar ledger/peak VRAM.
- [ ] A/B real: GPU frame ms, NR ms, FPS, 1% low, p95/p99, MFG pacing.
- [ ] Validar menu/config/recovery e rotas anunciadas.
- [ ] Substituir build_dist/installer dependentes de OptiScaler.
- [ ] Remover `apply_to_optiscaler.py` e outros arquivos de transição quando não forem mais necessários; não gastar refactor em código que será apagado.
- [ ] Modularizar build/install first-party para <=300 linhas por arquivo.
- [ ] Confirmar que CUDA/shaders/tests/tools ativos também cumprem o cap.
- [ ] Manter OptiScaler como fallback/referência durante preview, fora do novo runtime.
- [ ] Definir rollback objetivo.

## Revisão obrigatória

- [ ] A/B equivalente.
- [ ] Resultado negativo vira blocker/limitação.
- [ ] Installer não deixa proxies conflitantes.
- [ ] Nenhum arquivo é isentado do cap por ser “só teste/tool”.
- [ ] Vendor/generated/fixture são as únicas exceções formais.

## Validação rápida

- [ ] Harness suite antes da RC.
- [ ] Install/upgrade/uninstall.
- [ ] Hardware real rotas prioritárias.
- [ ] Stress reset/reconfigure/MFG.
- [ ] LOC checker final limpo.

## Gate

- [ ] Rotas anunciadas Hardware-qualified.
- [ ] 0 steady-state allocation/resource creation normal.
- [ ] VRAM equivalente sem regressão.
- [ ] Host CPU abaixo do baseline.
- [ ] Packaging sem dependência OptiScaler.
- [ ] **Zero first-party handwritten code file >300 linhas.**
- [ ] Rollback/fallback testado.

## Próxima fase

Fim.
