# Fase 23 — A/B final e cutover do OptiScaler

## Objetivo

Trocar host/distribuição após performance, packaging e regra estrutural completas.

## Dependências

Fase 22.

## Fora de escopo

- Apagar fallback cedo
- Claim GPU sem hardware

## Implementação

- [ ] Comparar CPU p50/p95/p99, allocations, locks, creates, queries e copies.
- [ ] Comparar VRAM e A/B real de GPU/FPS/1% low/pacing.
- [ ] Validar menu/config/recovery.
- [ ] Substituir build_dist/installer dependentes de OptiScaler.
- [ ] Remover `apply_to_optiscaler.py` e `OptiScalerAdapter` em vez de desperdiçar split se já estiverem obsoletos.
- [ ] Split `InstallerState.cpp` em **manifest/validation** e **transaction snapshot/restore** se permanecer ativo.
- [ ] Modularizar `build_dist.ps1` por build/bootstrap/package se permanecer >300.
- [ ] Confirmar CUDA/shaders/tests/tools/build/install ativos <=300.
- [ ] Manter OptiScaler apenas como referência/fallback durante preview.
- [ ] Definir rollback objetivo.

## Revisão obrigatória

- [ ] A/B equivalente.
- [ ] Resultado negativo = blocker/limitação.
- [ ] Installer não deixa proxies conflitantes.
- [ ] “Só teste/tool” não é exceção.
- [ ] Vendor/generated/fixture são únicas exceções formais.

## Validação rápida

- [ ] Harness suite.
- [ ] Install/upgrade/uninstall.
- [ ] Hardware real.
- [ ] Stress reset/reconfigure/MFG.
- [ ] LOC checker final.

## Gate

- [ ] Rotas anunciadas Hardware-qualified.
- [ ] Performance/VRAM gates cumpridos.
- [ ] Packaging sem OptiScaler.
- [ ] **Zero first-party handwritten code file >300 linhas.**
- [ ] Rollback testado.

## Próxima fase

Fim.
