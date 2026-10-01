# Fase 2B — checkpoint para retomada

Estado em 2026-10-01: **pausada, não concluída**.
Checkout: `D:\Users\tcfialho\Documents\NRFusion`.
Branch: `standalone/integration`; base deste registro: `315044a`.

## Regras acordadas

- Manter micro otimizações com ganho medido, qualidade preservada e sem impedir ganhos maiores.
- Registrar separadamente ganho da operação, NR/MFG total e frame completo; não somar percentuais.
- Exact permanece padrão. Diferenças gráficas com ganho expressivo podem justificar opção Experimental, desligada por padrão.
- Stock continua fallback e vencedor quando replacements são mais lentos no workload real.
- Build, replay e testbed não equivalem a qualificação em gameplay comercial.

## O que está integrado e comprovado

- `695f86b`: elimina a gravação descartada de `HdrCopy` no encode e duas transições associadas.
- Mantém a cópia original quando há consumidores: alvo UAV, crop ou histórico across-RR.
- Shader original permanece fallback se o PSO opcional não puder ser criado.
- Preparação p50: 1080p `0,025088 → 0,023552 ms`; 1440p `0,043008 → 0,038912 ms`; 4K `0,1024 → 0,083968 ms`.
- Em 4K: cerca de 18% na preparação, 18,432 µs e 14.745.600 bytes de gravações evitadas por frame.
- **Ganho consistente no NR total ou no frame completo não foi comprovado.**
- 3.480 pares de frames com hashes RGBA iguais; movimento, cena estática, três resoluções, reset/recreation e mudança de resolução de entrada.
- 60 casos RGBA16F comparados byte a byte em WARP e RTX 4050; modos de encode, exposição e valores extremos.
- Relatório: `benchmarks/phase2b/2026-10-01-unused-keep-elimination/summary.json`.

## Experimentos que não devem ser ativados

- Batching da cadeia projection/expand/contract: replay isolado melhorou, mas integração real causou device hung `0x887a0006`; causa não comprovada. Código da tentativa removido.
- FFN shape288: replay isolado melhorou ~1,8%, mas NR/frame reais pioraram; stock mantido.
- Swin 8h: capture de três execuções reais preserva duas alocações completas e aliases; stock replay passa antes/depois de 350 repetições.
- Swin cache CG: Exact nas capturas, mas ganho inicial não se reproduziu nos pares normal/inverso; não integrado.
- Swin SM86: não Exact e ~234,5 µs contra ~101,4 µs stock no replay; não oferece vantagem para opção Experimental.
- W4A8 anterior: não produção, não Exact e mais lento; não reparar incrementalmente nem habilitar.
- Relatórios em `benchmarks/phase2b/2026-10-01-swin8-qualification`, `2026-10-01-rejected-runtime-batch` e `2026-10-01-rejected-shape288`.

## Checklist restante

- [ ] B1: atualizar ranking com profiling real por workload/resolução; completar shapes, tráfego, relações producer/consumer e impacto esperado.
- [ ] B2: testar fusions seguras das hot chains; comparar stock/separate/fused, preservar rounding e sincronização e medir no runtime.
- [ ] B3/B7: testar variantes dos maiores consumidores, occupancy/registers/spills; Swin 8h usa 166 registers/thread, 256 threads/block e 16 KiB shared, limitado a uma CTA/SM por registers.
- [ ] B3/B11: implementar/reutilizar seleção e cache apenas para candidatos qualificados; incluir stock e invalidar por GPU/driver/runtime/identity/shape/precision/tier.
- [ ] B4/B6: avaliar outras materializações e epilogues; manter a eliminação de `HdrCopy` já integrada.
- [ ] B5: obter baseline real de MFG e avaliar técnicas Exact de sdli em SM89 com correctness e A/B próprios.
- [ ] B8: avaliar patches PTX/SASS Exact apenas com assinatura/pattern, domínio provado, replay e fallback.
- [ ] B9: reconsiderar reduced precision/W4A8 somente diante de evidência nova de ganho expressivo; Experimental opt-in.
- [ ] B10: medir trabalho independente antes de overlap; reutilizar scheduler/fences existentes e qualificar critical path e caudas.
- [ ] B12: comparar stock vs conjunto Exact em NR e MFG; medir p50/p95/p99, CPU overhead, VRAM/workspace e frame GPU.
- [ ] B12: ampliar validação temporal/visual para gameplay comercial, rotação, disocclusion, thin geometry, movimento e UI; mais hardware Ada quando disponível.

## Primeira ação ao retomar

Conferir Git, payload hashes, GPU/driver e baseline antes de editar. Próximo candidato pequeno: verificar se o encode de RGBA8 UNORM é identidade e se o modelo pode ler a textura original sem cópia. **Não implementado**; houve somente investigação read-only. Começar desativado, provar domínio/ownership, comparar hashes de todos os frames e medir com hashing desabilitado. Preservar HDR, history, crop e fallback.

As capturas grandes estão em `.temp/phase2b/swin8-full-captures` (ignoradas pelo Git); metadados e medições estão commitados. Não transportar conclusões para outros checkouts. A exclusão local preexistente de `AGENT.md` não pertence ao trabalho e deve continuar preservada.

A geração do instalador é um checkpoint das otimizações mantidas, não a conclusão da Fase 2B. A pausa das otimizações permanece válida durante commit/push/empacotamento.
