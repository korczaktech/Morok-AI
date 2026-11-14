# Etapa 16 — Documentação e experimentos

## Objetivo

Tornar o desenvolvimento experimental do Morok AI auditável e reproduzível. Esta etapa organiza como uma hipótese é registrada, como uma campanha é executada, quais evidências são preservadas e como resultados positivos, negativos ou inconclusivos são comunicados.

A etapa não afirma que a G1 já possui qualidade linguística competitiva. A validação com corpus real/licenciado e hardware de referência continua dependendo de dados e execuções que ainda não foram fornecidos.

## Escopo entregue

- Protocolo de experimentação com hipótese, variável independente, controles, métricas e critérios de decisão.
- Modelo versionável de manifesto JSON para cada campanha.
- Convenções para proveniência, licença, hash e particionamento de corpus.
- Registro separado de configuração planejada, execução realizada e resultado observado.
- Estrutura para ablações e experimentos de escala de geração.
- Política para preservar resultados negativos e campanhas incompletas.
- Relato do resultado sintético já observado, sem extrapolação científica.
- Checklist de reprodutibilidade e revisão antes de publicar conclusões.

## Estrutura

- `experiments/README.md`: fluxo de trabalho geral.
- `experiments/templates/manifest.example.json`: manifesto-base de campanha.
- `experiments/ablations/README.md`: protocolo de ablações.
- `experiments/generation-scaling/README.md`: protocolo de escala de geração.
- `experiments/results/README.md`: regras e índice dos resultados.
- `docs/35-etapa-10-benchmark-validacao.md`: benchmark implementado e smoke test sintético da G1.
- `docs/23-build-testes-e-distribuicao.md`: requisitos de build, testes e observabilidade.

## Protocolo obrigatório

### 1. Formular uma hipótese falsificável

Cada experimento deve declarar:
- pergunta que pretende responder;
- hipótese e resultado que a refutaria;
- variável independente;
- variáveis controladas;
- métrica primária e métricas secundárias;
- critério de decisão definido antes da execução.

Não alterar várias dimensões simultaneamente quando o objetivo for atribuir causalidade a uma mudança.

### 2. Fixar dados e proveniência

Antes do treino:
- registrar origem, licença e autorização de uso;
- registrar hash SHA-256 do conjunto bruto e dos arquivos de split;
- documentar normalização, filtragem, deduplicação e exclusões;
- dividir por documento, nunca por fragmentos aleatórios do mesmo documento;
- manter treino, validação e teste isolados;
- usar validação para seleção e teste apenas para avaliação final.

Não inserir corpus privado, protegido ou de licença desconhecida no repositório. Manter apenas metadados, hashes e instruções para obter os dados autorizados.

### 3. Fixar ambiente e configuração

Registrar commit Git, versão do modelo, compilador, versão, flags, CMake, sistema operacional, arquitetura, CPU, RAM disponível, limites de processo e configuração integral do treino. Registrar sementes explicitamente.

Para comparar configurações, usar as mesmas divisões de dados e sementes correspondentes. Repetir com múltiplas sementes; não escolher apenas a execução mais favorável.

### 4. Executar e preservar

Criar um manifesto por campanha. Guardar o JSONL original emitido pelo executável, logs, status de saída, checkpoint quando permitido e hashes dos artefatos. Nunca sobrescrever resultados: uma nova execução recebe um identificador e um arquivo novo.

Uma execução interrompida deve ser marcada como incompleta, com motivo e último estágio alcançado. Ausência de resultado não é resultado negativo nem aprovação.

### 5. Analisar

Para BPB, informar conjunto avaliado, número de bytes-alvo e checkpoint. Para tempo e throughput, informar denominador e escopo medido. Para RSS, identificar se é pico do processo ou memória total do dispositivo.

Apresentar todas as sementes, dispersão (média, desvio-padrão e intervalo quando aplicável) e resultados por configuração. Não misturar resultados de hardware ou corpus diferentes sem estratificação.

Uma diferença numérica isolada não demonstra significância estatística nem superioridade geral. Comparações com baseline devem explicitar diferenças de capacidade e contexto.

### 6. Publicar conclusão

Toda conclusão deve ser classificada como:
- **suportada**: os critérios pré-definidos foram satisfeitos nas condições declaradas;
- **não suportada**: os dados contradizem a hipótese;
- **inconclusiva**: amostra, qualidade dos dados ou execução insuficiente;
- **não executada**: existe apenas plano/manifesto.

Publicar limitações, desvios do protocolo e resultados negativos junto aos positivos.

## Campanhas prioritárias

1. Reproduzir o benchmark G1 com corpus real/licenciado, splits fixos e múltiplas sementes.
2. Comparar G1 com baseline de frequência e, depois, baseline contextual de capacidade claramente descrita.
3. Executar ablações controladas do estado fast/slow, BPTT e cabeça preditiva, apenas quando cada variante puder ser isolada sem alterar outras dimensões.
4. Medir geração com diferentes limites de bytes e tamanhos de prompt, registrando latência por byte, throughput, RSS e validade UTF-8.
5. Medir memória em hardware de referência com limite real de 4 GB; não inferir isso a partir de VM de CI.

## Resultado já conhecido e interpretação

O smoke test documentado em `docs/35-etapa-10-benchmark-validacao.md` usou corpus sintético de seis documentos e uma época por configuração. Nele, o baseline softmax-256 unigram obteve BPB de teste 4,9958, enquanto a G1 ficou entre 7,9658 e 8,8871. Portanto, nesse fixture, o baseline foi melhor. Isso é uma evidência de que a G1 ainda não demonstrou vantagem preditiva, mas não permite concluir qualidade em linguagem natural, generalização ou desempenho em corpus real.

## Critérios de aceitação da etapa

- [x] Protocolo reproduzível documentado.
- [x] Manifesto de campanha versionável disponibilizado.
- [x] Estruturas de ablação, escala de geração e resultados documentadas.
- [x] Política explícita de proveniência, licenças, hashes e splits.
- [x] Resultado sintético existente preservado com limitações.
- [x] README e índice documental integrados.
- [ ] Campanha real/licenciada executada: depende de corpus autorizado.
- [ ] Validação em hardware físico de 4 GB: depende de equipamento de referência.

Os dois últimos itens são validações científicas posteriores, não podem ser marcados como executados sem os dados e o hardware correspondentes.

## Estado

A infraestrutura documental e o protocolo de experimentos da etapa 16 estão implementados. A execução científica com corpus real e hardware de referência permanece pendente e está explicitamente separada da conclusão desta etapa de engenharia.
