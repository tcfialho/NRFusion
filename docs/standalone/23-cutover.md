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
- [ ] Remover `apply_to_optiscaler.py` e `OptiScalerAdapter` em vez de refatorá-los se já obsoletos.
- [ ] Split `InstallerState.cpp`: manifest/validation e transaction snapshot/restore.
- [ ] `installer/NRFusion.nsi` (~466 linhas) deve ser dividido por UI/config/install-uninstall usando includes pequenos.
- [ ] `build_dist.ps1` (~395 linhas) deve ser dividido por bootstrap/build/package se ainda ativo.
- [ ] CMake modular continua <=300 por arquivo.
- [ ] Confirmar CUDA/shaders/tests/tools/build/install ativos <=300.
- [ ] Manter OptiScaler apenas como referência/fallback durante preview.
- [ ] Definir rollback objetivo.

## Revisão obrigatória

- [ ] A/B equivalente.
- [ ] Resultado negativo = blocker/limitação.
- [ ] Installer não deixa proxies conflitantes.
- [ ] Includes de NSIS/CMake não são usados como dump arbitrário para burlar o cap.
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
