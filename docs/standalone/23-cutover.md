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
- [ ] Remover patcher/adapter obsoletos em vez de refatorá-los.
- [ ] `InstallerState.cpp`: separar manifest/validation de transaction snapshot/restore.
- [ ] `NRFusion.nsi`: separar UI/config, install e uninstall/restore em includes coesos, mantendo um único installer.
- [ ] `build_dist.ps1`: separar bootstrap, build e package mantendo um único fluxo de distribuição.
- [ ] CMake modular continua <=300 por arquivo e sem targets/jobs duplicados.
- [ ] Confirmar CUDA/shaders/tests/tools/build/install ativos <=300.
- [ ] Manter OptiScaler como fallback/referência durante preview.
- [ ] Definir rollback objetivo.

## Revisão obrigatória

- [ ] A/B equivalente.
- [ ] Resultado negativo = blocker/limitação.
- [ ] Installer não deixa proxies conflitantes.
- [ ] Includes NSIS/CMake não são dumps para burlar cap.
- [ ] “Só teste/tool” não é exceção.
- [ ] Packaging modular não multiplica builds pesados.

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
