# Fase 23 — A/B final e cutover do OptiScaler

## Status

**EM ANDAMENTO.** O corte agora distribui um carrier real somente para D3D11 x64:
`version.dll` inicia a captura D3D11, que entrega o trabalho ao `NRFusionHost64.exe`.
As demais rotas permanecem qualificação de harness; elas não possuem hook distribuído e
não podem ser anunciadas como suporte de produto.

## Objetivo

Trocar host/distribuição após performance, packaging e regra estrutural completas.

## Dependências

Fase 22.

## Fora de escopo

- Apagar fallback cedo
- Claim GPU sem hardware
- Anunciar um carrier sem proxy e caller distribuídos

## Implementação

- [x] Remover o proxy inerte: `nrfusion_proxy.dll` agora contém o hook D3D11 e inicia o modo Neural.
- [x] Restringir o loader a `version.dll`, único conjunto de exports que o binário encaminha.
- [x] Instalar Host64 e os dois sidecars ao lado do Host, conforme o loader os procura.
- [x] Manter manifesto e `InstallerState` no mesmo layout do instalador x64.
- [x] Atualizar `GameProbe` para aceitar apenas D3D11 x64 que importa `version.dll`.
- [x] Atualizar os contratos de distribution/UX para o fluxo standalone.
- [ ] Exercitar install, upgrade e uninstall com os sidecars aprovados e um jogo D3D11 real.
- [ ] Integrar hooks distribuídos para D3D12, Vulkan, OpenGL, D3D10, D3D9Ex e MFG antes de anunciá-los.
- [ ] Construir e qualificar o carrier Win32 antes de reabrir suporte x86.

## Revisão obrigatória

- [x] O proxy instalado e seus exports correspondem ao nome selecionado.
- [x] O Host e os sidecars entram no mesmo layout que o manifesto registra.
- [x] O instalador falha fechado quando a API, bitness ou import do proxy não é suportado.
- [ ] A/B equivalente em jogo real do fluxo distribuído.
- [ ] Install/upgrade/uninstall do pacote oficial.
- [ ] Hardware-qualified só para rotas com hook distribuído.

## Validação atual

- [x] `nrfusion_proxy`, `nrfusion_game_probe_tests` e `nrfusion_capture32` compilam no build Windows.
- [x] `nrfusion_game_probe_tests`, `nrfusion_capture32_transport_timeout_tests` e
  `nrfusion_capture32_roundtrip_test` passam.
- [x] `tests/test_dist_contract.sh` e `tests/test_ux_contract.sh` passam no Git Bash.
- [x] `python tools/check_source_size.py changed` reporta zero violações.
- [x] CTest Windows: 73 testes passaram e 1 foi skipped por ausência de timing source aposentado.
- [ ] Fluxo de instalação real deve ser repetido em jogo D3D11 x64.

## Gate

- [x] D3D11 x64 tem proxy, hook e Host64 distribuídos no mesmo contrato de pacote.
- [ ] D3D11 x64 validado em jogo real instalado.
- [ ] Outras rotas têm hooks distribuídos e qualificação de produto.
- [ ] Rollback do instalador validado com o pacote oficial.

## Próxima ação

Criar o pacote com `RuntimePath` e `ForwarderPath` aprovados, validar o ciclo de
instalação em jogo D3D11 x64 e só então ampliar a matriz de produto.
