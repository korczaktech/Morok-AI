# G3 — contrato arquitetural e integração

## 1. Regra estrutural

A G3 utiliza o motor Morok existente. Não será criado um segundo Core, um segundo runtime de inferência ou uma cópia do motor dentro de `Gerações/G3/shared/`.

A infraestrutura compartilhada permanece na biblioteca pública `Morok::Core`. O contrato neutro de execução de modelos está em:

- `include/morok/model/model_backend.hpp`

Esse contrato descreve como o runtime conversa com um backend neural, sem conhecer sua arquitetura interna, pesos, dimensões ou implementação matemática.

A G3 implementará seu backend arquitetural dentro do próprio diretório da geração e o conectará ao contrato público. A implementação de GRU, RMSNorm, projeção, weight tying e kernels específicos da G3 pertence ao backend da G3; isso não autoriza duplicar o runtime ou o Core.

## 2. O que já existe e o que foi integrado

| Componente | Local | Responsabilidade |
|---|---|---|
| Core compartilhado | `Morok::Core` | Tipos, erros, UTF-8, memória e subsistemas neutros |
| Contrato de backend | `morok/model/model_backend.hpp` | Interface comum para execução incremental |
| Motor neural G1 | `Morok::G1` | Arquitetura recorrente de duas escalas já existente |
| Adaptador G1 | `morok/model/g1_backend.hpp` | Expõe o motor G1 pelo contrato comum |
| Motor Tsukuyomi G2 | `Morok::G2::Tsukuyomi` | Implementação G2 existente, preservada e isolada |
| Backend G3 | `Gerações/G3/` | Será implementado nas próximas etapas usando o contrato |

O adaptador G1 não copia nem substitui o motor. Ele encaminha as operações ao forward pass existente. O objeto de parâmetros continua pertencendo ao chamador e deve permanecer vivo e imutável enquanto o adaptador estiver em uso.

A G2 permanece sem dependência da implementação privada da G1. Nenhum código da G2 precisa ser migrado para concluir esta etapa.

## 3. Contrato público

`morok::model::ModelBackend` define:

- `descriptor()`: identidade da geração e arquitetura, vocabulário, contagem de parâmetros e tamanho/alinhamento do estado.
- `reset_state(state)`: inicializa o estado de uma sessão independente.
- `consume_byte(state, byte)`: incorpora um byte observado sem compartilhar estado entre sessões.
- `next_token_log_probabilities(state, output)`: produz uma log-probabilidade por elemento do vocabulário.

A fachada `morok::model::ModelSession`, também publicada pelo Core, conecta um
backend a um buffer de estado fornecido pelo chamador. Ela valida descritor,
tamanho e alinhamento, exige reset antes de entrada/inferência e impede cópia
acidental do objeto de sessão. Não possui pesos, kernels, gerador ou otimizador:
é apenas o caminho comum de ciclo de vida e despacho para o backend existente.

O estado é opaco para o Core. O chamador aloca e possui o buffer; o backend só interpreta o estado criado por sua própria implementação. O tamanho exato é declarado no descritor. O alinhamento é informado para que futuros backends possam usar armazenamento especializado.

As saídas são log-probabilidades, não logits arbitrários. Entradas não suportadas pelo vocabulário devem ser representadas por menos infinito. A quantidade de saídas precisa corresponder exatamente ao tamanho do vocabulário declarado.

As operações retornam `morok::common::Status`. Erros de tamanho, estado inválido e falhas numéricas não devem resultar em atualização parcial do estado ou da saída.

## 4. Identidade de arquitetura

O contrato não presume que todas as gerações tenham a mesma arquitetura.

- G1: geração `1`, arquitetura `0x0101`, vocabulário de 256 bytes, 1.000 parâmetros e estado recorrente próprio.
- G3: geração `3`, arquitetura `0x0302`, vocabulário de 261 IDs, exatamente 100.000 parâmetros treináveis e estado recorrente de 660 bytes em FP32 por sessão (97 + 68 valores).

O identificador G3 é uma reserva para a arquitetura GRU-RMSNorm-Weight-Tying. Deve permanecer estável depois que o formato de checkpoint for publicado. A contagem real de parâmetros deverá ser calculada pela implementação e confrontada com a especificação antes de aprovar a etapa neural.

O contrato aceita vocabulários e estados de tamanhos diferentes. Portanto, a G1 não precisa ser alterada para imitar a GRU da G3, e a G3 não precisa copiar o código da G1.

## 5. Limites de responsabilidade

### Core Morok
- Define tipos e contratos neutros.
- Não contém parâmetros privados de uma geração.
- Não contém kernels exclusivos da G1, G2 ou G3.
- Não escolhe hiperparâmetros de treinamento de uma geração.

### Backend de cada geração
- Implementa seu forward pass e estado recorrente.
- Informa dimensões, arquitetura e vocabulário.
- Converte sua distribuição de saída para log-probabilidades.
- Valida seu próprio estado e suas restrições numéricas.

### Treinamento
- Será conectado em etapa posterior.
- Não faz parte da interface mínima de inferência desta etapa.
- SFT, BPTT, otimizador, RL e persistência terão contratos próprios, sem acoplar o Core a um algoritmo específico.

### Perfis G3
Mokosh, Tsukuyomi, Artemis e Apollo serão políticas/perfis construídos sobre a infraestrutura e o backend da G3. Não terão cópias independentes do Core ou do runtime.

## 6. Compatibilidade e isolamento

- A G1 mantém seus executáveis, parâmetros, treinamento, checkpoint e resultados atuais.
- O adaptador G1 é aditivo e usa as funções existentes de avanço e avaliação.
- A G2 continua compilando como `Morok::G2::Tsukuyomi`, sem importar fontes privadas da G1.
- O contrato comum é instalado junto aos cabeçalhos públicos de Morok.
- A G3 depende de `Morok::Core` e do contrato público, nunca de caminhos privados da G1/G2.
- Mudanças futuras no contrato exigem testes de compatibilidade e versionamento explícito.

## 7. Critérios de conclusão da Etapa 1

- [x] Contrato neutro de backend disponível em cabeçalho público.
- [x] Adaptador da G1 utiliza o motor existente, sem duplicá-lo.
- [x] Estado de sessão é fornecido pelo chamador e não é compartilhado.
- [x] Saída padronizada como log-probabilidades por item de vocabulário.
- [x] Testes cobrem descritor, reset, transições incrementais, normalização e buffers inválidos.
- [x] CMake integra o adaptador à biblioteca G1 existente.
- [x] G2 permanece isolada da implementação privada da G1.
- [x] Documentação deixa explícito que a G3 não criará um motor/Core paralelo.
- [x] CI da G3 valida contrato, adaptador G1, G2 e isolamento arquitetural.

A Etapa 1 não implementa a GRU G3, treinamento SFT/RL ou inferência final. Essas entregas pertencem às etapas posteriores.


## 8. Representação linguística G3 — Etapa 2

A representação byte-level foi integrada à superfície compartilhada do Core,
sem criar um subsistema de tokenização paralelo dentro da G3:

- Cabeçalho público: `include/morok/lang/byte_tokenizer.hpp`.
- Implementação: `src/lang/byte_tokenizer.cpp`.
- API: `morok::lang::ByteTokenizer`.
- Vocabulário: 261 IDs estáveis — bytes 0–255, PAD 256, BOS 257, EOS 258,
  USR 259 e BOT 260.
- Mensagens: `[BOS?][USR|BOT][bytes UTF-8][EOS?]`; o método de append permite
  compor vários turnos sem inserir BOS/EOS adicionais.
- Entrada inválida: substituição por U+FFFD por padrão, com opção estrita de
  rejeição. A ocorrência de substituição é devolvida para diagnóstico.
- Normalização: Unicode NFC por padrão, configurável para desativação. Não há
  remoção automática de acentos, pontuação, caixa ou espaços.
- Saída: IDs de byte são reconstruídos e validados como UTF-8 antes da entrega;
  EOS encerra a decodificação e outros tokens de controle são rejeitados. O limite
  máximo de bytes recua até uma fronteira UTF-8 válida e informa se houve corte.
  Substituições de bytes inválidos na saída também são sinalizadas no resultado.
- NFC é implementada pela biblioteca ICU (componente uc), não por uma tabela
  Unicode parcial ou uma normalização artesanal.

O Core continua responsável apenas por contratos e infraestrutura genéricos.
O tokenizer é um utilitário compartilhado de representação textual, não um
novo motor de inferência. Treinamento, dataset, SFT, RL e a arquitetura neural
G3 permanecem fora do escopo desta etapa.

### Critérios de conclusão da Etapa 2

- [x] IDs byte-level e cinco tokens especiais distintos e estáveis.
- [x] Conversão de UTF-8 em IDs de byte, preservando os bytes do texto normalizado.
- [x] Marcadores BOS, EOS, USR e BOT e composição de múltiplas mensagens.
- [x] Validação estrita ou substituição configurável de UTF-8 inválido.
- [x] NFC configurável, habilitada por padrão, via ICU.
- [x] Validação de UTF-8 na decodificação antes de entregar texto.
- [x] Limite de saída que recua até uma fronteira UTF-8 e sinaliza truncamento.
- [x] Testes de vocabulário, português acentuado, NFC, dados inválidos,
      políticas de rejeição, tokens de controle e round-trip.
- [x] CMake e workflow G3 integrados ao Core existente.


## 9. Módulo de aprendizado por reforço — treinamento independente

A implementação de RL pertence a `Gerações/G3/rl/` e reutiliza
`Morok::Core`. Não cria Core, runtime de inferência, estado de modelo ou
armazenamento de pesos paralelo.

### 9.1 Contrato de política

- Cabeçalho: `rl/include/morok/g3/rl/reinforce.hpp`.
- Implementação: `rl/src/reinforce.cpp`.
- Testes: `rl/tests/test_reinforce.cpp`.
- A política fornece uma distribuição normalizada de 261 IDs para cada
  contexto/prefixo.
- O algoritmo bloqueia PAD, BOS, USR e BOT como tokens de resposta; bytes e
  EOS permanecem disponíveis.
- O caminho principal não recebe política de referência. Uma sobrecarga opcional
  aceita uma política congelada apenas quando o chamador ativa KL explicitamente;
  o beta padrão é zero e o CLI independente não carrega SFT.
- A política treinável recebe gradientes de logits acumulados para o lote e
  aplica uma única atualização de otimizador com gradientes médios sobre as candidatas do lote. Os pesos e o otimizador continuam
  pertencendo ao backend neural da geração.

### 9.2 REINFORCE e amostragem

- Até quatro candidatas por pergunta, configurável.
- Amostragem com temperatura e Top-K configuráveis; defaults experimentais
  temperatura 0,7 e Top-K 10.
- Seed determinística configurável.
- REINFORCE usa `A = recompensa - baseline_categoria`.
- Baseline exponencial móvel por categoria, com coeficiente configurável
  (default 0,10).
- Gamma default 1,0; ponderação temporal aplicada por token.
- Gradientes são acumulados para todas as candidatas antes da chamada única
  `apply_batch_gradients`, evitando que candidatas do mesmo lote sejam
  geradas com pesos parcialmente atualizados.

### 9.3 Recompensas auditáveis

A rubrica expõe quatro componentes separados: correção, aderência à intenção,
coerência e formato. Pesos default: 0,70 / 0,15 / 0,05 / 0,10; a soma deve ser
1 dentro da tolerância numérica. Cada componente deve estar em [-1, +1]. A
recompensa composta é limitada a [-1, +1].

Penalidades configuráveis:
- resposta vazia e ausência de EOS;
- repetição de bytes consecutivos e de sequências de bytes repetidas;
- cópia de sequência contígua de bytes do prompt;
- excesso de comprimento somente quando um limite justificado for configurado.

O avaliador atualmente implementado é lexical e determinístico. Para cada
prompt com resposta de referência, calcula: (1) similaridade normalizada por
distância de edição para `correctness`; (2) F1 de bigramas para `intent`;
(3) componente de coerência neutro, pois comprimento não comprova coerência;
e (4) resposta não vazia para `format`. Esses nomes são rótulos da interface, não evidência de
compreensão semântica. Respostas corretas com redação diferente podem receber
nota baixa, enquanto respostas lexicalmente parecidas podem estar erradas.
A recompensa atual serve apenas como baseline experimental de proximidade
textual, não como certificação de qualidade, factualidade ou alinhamento.

`audit_evaluator` verifica metadados e intervalos fornecidos pelo chamador; isso
não prova independência humana nem validade semântica. No CLI atual, os casos
de auditoria são verificações sintéticas de resposta exata e resposta vazia,
não um conjunto humano independente. Antes de usar RL como treino de qualidade,
é necessário adicionar casos rotulados por pessoas, incluindo paráfrases
corretas, respostas plausíveis porém falsas e exemplos de reward hacking.
Até lá, relatórios de recompensa devem ser identificados como métricas
lexicais experimentais. Não há penalidade automática de comprimento por padrão.

### 9.4 Independência do SFT e regularização opcional

O fluxo principal de RL é independente do SFT. O executável
`morok-g3-reinforce` recebe somente um dataset RL, o caminho do artefato RL de
saída, o relatório e parâmetros opcionais de épocas/seed. Ele inicializa uma
nova política G3 a partir da seed e não carrega pesos, checkpoints, snapshots,
estado do otimizador ou dataset do SFT.

A penalidade KL contra uma política congelada permanece disponível apenas como
opção experimental na API. Seu beta padrão é zero. Quando beta é maior que zero,
o chamador precisa fornecer explicitamente uma política de referência; o CLI
independente não fornece essa referência e não depende dela.

O avaliador lexical utiliza respostas de referência pertencentes ao dataset RL.
Isso é sinal de recompensa do próprio experimento, não carregamento ou
dependência do pipeline SFT. A separação de treino, validação e teste ocorre por
`group_id`; somente o split de treino atualiza os pesos.

### 9.5 Proteção contra reward hacking

O módulo mede e penaliza resposta vazia, ausência de EOS, repetição e cópia da
pergunta; o comprimento só é penalizado com limite explicitamente configurado.
O avaliador implementado usa distância de edição e F1 de bigramas; coerência
fica neutra e formato verifica resposta não vazia. Esses sinais não medem
semântica, factualidade ou intenção de forma confiável e não devem ser
apresentados como julgamento semântico.

Os testes cobrem resposta vazia, genérica, repetida, exploração de avaliador,
penalidade de EOS, KL opcional e treinamento sem referência SFT. Os casos de
auditoria do CLI são sintéticos e derivados do dataset, não um conjunto humano
independente. Uma avaliação semântica exige rótulos humanos independentes.

### 9.6 Comparação e métricas

`compare_sft_and_rl` permanece como utilitário opcional de avaliação
comparativa, não como etapa do treinamento RL. O fluxo principal avalia a
política RL no split de teste e relata recompensa média, comprimento médio,
taxa de respostas vazias e taxa de repetição. A comparação com um snapshot SFT,
quando desejada, é uma análise posterior separada.

O estado recuperável do treinador guarda configuração, baseline por categoria,
estado do gerador aleatório e identidades da política, referência opcional e
avaliador. O adaptador mantém o estado do otimizador durante a execução atual;
persistência e retomada completas do estado do otimizador RL pela CLI ainda
precisam ser implementadas.

### Estado de implementação do módulo RL

- [x] Política treinável G3 e inicialização independente por seed.
- [x] CLI RL sem argumento ou carregamento de modelo SFT.
- [x] Dataset RL próprio com separação por grupo.
- [x] REINFORCE com baseline EMA por categoria.
- [x] Amostragem configurável com até quatro candidatas, Top-K, temperatura e seed.
- [x] Composição de recompensa e penalidades explícitas.
- [x] Auditoria versionada do avaliador lexical.
- [x] KL opcional contra referência externa, desativada por padrão.
- [x] BPTT pelas duas GRUs e atualização dos 100.000 parâmetros.
- [x] Exportação do artefato FP32 com metadados completos da arquitetura.
- [x] Teste de integração RL sem dependência do alvo SFT.
- [ ] Checkpoint RL completo na CLI, incluindo pesos, momentos AdamW, baseline,
      RNG, posição do dataset e fingerprints para retomada exata.
- [ ] Avaliação semântica com conjunto humano independente.
- [ ] Execução de treino com corpus de produção revisado e benchmark de hardware.

## 10. Inferência e exportação — Fase 4

A Fase 4 adiciona utilitários G3 específicos em `Gerações/G3/inference/`,
reutilizando `Morok::Core` e o contrato público `ModelBackend`. Não cria
outro runtime nem altera os fontes das gerações anteriores.

### 10.1 Decodificação

- `generator.hpp/.cpp` executa a geração incremental por uma sessão fornecida
  pelo chamador.
- Temperatura configurável, Top-K configurável (zero desativa), softmax estável,
  EOS como parada e bloqueio de PAD/BOS/USR/BOT como conteúdo.
- Limites de entrada e saída, timeout, cancelamento e política para resposta
  vazia são explícitos.
- A saída passa pelo `ByteTokenizer`, com validação UTF-8 e política de
  substituição/rejeição configurável.
- O estado da sessão continua pertencendo ao chamador; cada chamada exige seu
  próprio buffer de estado.

### 10.2 Quantização INT8

- `quantization.hpp/.cpp` implementa quantização simétrica por linha para
  valores INT8 no intervalo [-127, 127], com uma escala FP32 por linha.
- Linhas zeradas usam escala 1.0; NaN, infinito, dimensões inconsistentes e
  escalas inválidas são rejeitados.
- A estrutura quantizada é armazenada uma vez. A futura GRU deverá referenciar
  a mesma estrutura para lookup de embedding e projeção de saída (weight tying),
  sem criar cópias sincronizadas.

### 10.3 Artefato .morok

- `model_artifact.hpp/.cpp` define cabeçalho versionado de 64 bytes, campos
  little-endian explícitos, identidade G3/arquitetura, precisão, dimensões,
  estado, contagem real de parâmetros, tamanho do payload, escalas e CRC32.
- `save_model_artifact_atomic` grava em arquivo temporário, relê e valida o
  artefato antes do rename final; falha de escrita/validação/rename preserva o
  destino anterior. `read_model_artifact` limita o tamanho antes de alocar.
- O carregador rejeita magic/versão/arquitetura incompatíveis, dimensões fora
  dos limites, payload truncado/excessivo, layout de precisão incorreto,
  bytes reservados não nulos e checksum inválido.
- FP32 exige exatamente quatro bytes por parâmetro. INT8 exige um byte por
  parâmetro mais quatro bytes por escala.
- A contagem é informada pela estrutura real e deve ser exatamente 100.000 para a arquitetura `0x0302`; o serializador valida dimensões, identificador, contagem e payload.

### 10.4 Medição e isolamento

- `benchmark.hpp/.cpp` mede latências por execução, mediana, p95,
  throughput de bytes, tamanho do estado por sessão, RSS antes/depois e pico
  de RSS quando o sistema operacional expõe esses dados.
- O benchmark recebe um backend real como argumento; os testes atuais usam
  backend determinístico de fixture apenas para validar o instrumento.
- Testes executam oito sessões em paralelo com backend compartilhado e estados
  independentes.

### 10.5 Estado de validação

- [x] Decodificação Top-K/temperatura, EOS e filtros de tokens de controle.
- [x] Limites, timeout/cancelamento e política de resposta vazia.
- [x] Quantização/dequantização INT8 simétrica por linha.
- [x] Serialização/leitura do artefato e validação CRC32/compatibilidade.
- [x] Testes de corrupção, layout FP32/INT8 e isolamento de sessões.
- [x] Harness de benchmark com mediana, p95, throughput e memória residente.
- [ ] Equivalência FP32/INT8 medida com pesos reais da GRU G3.
- [ ] RAM e latência de produção medidas em hardware de referência.
- [x] Weight tying integrado ao armazenamento real da GRU G3.
- [ ] Teste de concorrência com o backend neural G3 real.

A arquitetura neural e o weight tying já estão implementados. Permanecem pendentes
medições de produção e avaliação com pesos/corpus revisados. Fixtures sintéticas
validam contratos e infraestrutura, não a qualidade nem o desempenho do modelo.

## 10. Arquitetura neural G3 — implementação

O backend neural específico está em Gerações/G3/neural/ e implementa o
contrato morok::model::ModelBackend, sem criar um Core ou runtime paralelo.

- Cabeçalho: neural/include/morok/g3/neural/gru_backend.hpp.
- Implementação: neural/src/gru_backend.cpp.
- Testes: neural/tests/test_gru_backend.cpp.
- Biblioteca: Morok::G3::Neural.
- Arquitetura normativa: GRU de duas camadas, E=60, H1=97, H2=68, V=261, identificador `0x0302`.

A ordem das portas é fixa: update, reset, candidate. A convenção é
h_t = (1-z_t) * n_t + z_t * h_(t-1). Cada porta possui uma matriz de entrada,
uma matriz recorrente e um único vetor de bias. A porta candidate aplica o
reset ao estado anterior antes da multiplicação recorrente.

A primeira GRU recebe o embedding de 60 dimensões. RMSNorm com escala
aprendível e epsilon configurável é aplicada entre as camadas. A segunda GRU
recebe dimensão 97 e produz estado oculto de dimensão 68. O estado de sessão
contém os dois vetores ocultos FP32 (660 bytes), é fornecido pelo chamador e
não é compartilhado entre sessões.

A cabeça de saída projeta H2=68 para E=60 e calcula os logits com a transposta
da própria matriz de embedding, mais bias de saída independente. Não existe
uma segunda matriz de vocabulário: lookup e saída usam o mesmo vetor de pesos,
implementando weight tying por armazenamento único.

Inicialização:
- embedding, matrizes de entrada das portas e projeção: Xavier/Glorot;
- matrizes recorrentes: ortogonal por porta, via Gram-Schmidt;
- bias update: +1, após fixar a convenção acima;
- demais biases: zero;
- escala RMSNorm: um;
- seed e epsilon são configuráveis; a seed padrão é determinística.

A contagem é calculada somando os tamanhos reais dos tensores alocados. Na
configuração normativa `0x0302`, a implementação retorna exatamente 100.000
parâmetros: 15.660 embedding + 45.978 GRU 1 + 33.864 GRU 2 + 4.080 projeção
+ 60 bias da projeção + 97 escala RMSNorm + 261 bias de saída. A matriz de
saída reutiliza o embedding transposto e não adiciona outro tensor. O descritor
usa a contagem calculada e o construtor rejeita qualquer total diferente.

### Critérios da arquitetura neural

- [x] Embedding de dimensão 60.
- [x] GRU com duas camadas.
- [x] Estados ocultos de dimensão 97 e 68.
- [x] RMSNorm entre as camadas, com escala aprendível e epsilon configurável.
- [x] Projeção de saída 68 → 60 e logits no vocabulário de 261 IDs.
- [x] Weight tying com armazenamento único entre embedding e matriz de saída.
- [x] Inicialização Xavier/Glorot, recorrência ortogonal e biases documentados.
- [x] Contagem automática dos parâmetros a partir dos tensores reais.

Os testes verificam dimensões, inicialização determinística por seed,
ortogonalidade das matrizes recorrentes, bias da porta update, escala RMSNorm,
contagem real, normalização das log-probabilidades, compartilhamento do
embedding na saída, validação de dimensões e isolamento de estados entre sessões.

Nota histórica da entrega inicial: naquele estágio, somente o forward pass
incremental estava concluído. As etapas posteriores implementaram BPTT, gradientes,
AdamW, treinamento SFT e REINFORCE sobre os tensores reais da GRU G3.

---

## Revisão vigente — orçamento exato de 100.000 parâmetros

Esta seção substitui, para a arquitetura neural executável da G3, as dimensões e a contagem de referência descritas na seção anterior. A referência inicial E=64/H=128 e o identificador 0x0301 foram substituídos pela arquitetura 0x0302, com E=60, H1=97 e H2=68. A alteração é permitida pelo princípio de configuração explícita: dimensões e limites são valores configuráveis, não constantes imutáveis.

A arquitetura mantém duas camadas GRU completas, três portas por camada, RMSNorm com escala aprendível entre as camadas, projeção final e weight tying com armazenamento único.

Contagem:
- embedding 261×60: 15.660;
- GRU 1: entrada 3×97×60 (17.460), recorrência 3×97×97 (28.227), bias 3×97 (291), total 45.978;
- GRU 2: entrada 3×68×97 (19.788), recorrência 3×68×68 (13.872), bias 3×68 (204), total 33.864;
- RMSNorm: 97;
- projeção 60×68: 4.080;
- bias da projeção: 60;
- bias de saída: 261;
- matriz de saída: sem tensor adicional, pois utiliza a transposta do próprio embedding.

Total calculado: **100.000 parâmetros exatos**. O estado por sessão contém 97+68=165 valores FP32, totalizando 660 bytes.

O backend calcula o total a partir dos tamanhos reais dos tensores e rejeita no construtor qualquer total diferente de 100.000. O descritor também informa a contagem dinâmica. O identificador 0x0302 torna os artefatos anteriores 0x0301 incompatíveis; não se deve reinterpretar checkpoints antigos com as novas dimensões.

A especificação detalhada e os critérios de teste estão em [PARAMETROS-100K.md](PARAMETROS-100K.md).


---

## Revisão SFT — Etapa de dataset e treinamento supervisionado

O pipeline supervisionado da G3 está em `Gerações/G3/training/` e atualiza diretamente os tensores do backend `Morok::G3::Neural`. Não existe um segundo motor de inferência.

### Dataset e split

- Loader JSONL com `example_id`, `group_id`, `category`, `question`, `answer`, `source`, `language` e `dataset_version`.
- Campos opcionais: `quality`, `sensitive`, `question_variants` e `expansion_axes`.
- UTF-8 é validado e texto normalizado em NFC via ICU.
- IDs duplicados, registros incompletos e qualidade fora de [0,1] são rejeitados.
- Fingerprint determinístico FNV-1a de 64 bits inclui conteúdo e metadados; não é checksum criptográfico.
- Split 85/10/5 por grupos completos, seed configurável; exige pelo menos três grupos.
- Expansão/ruído só são aplicados no split de treino. Validação e teste permanecem sem perturbação; o split de teste não participa do ajuste.

### Expansão e perturbação

- Variantes curadas em `question_variants`.
- Eixos combinatórios em `expansion_axes`, associados a placeholders `{0}`, `{1}` etc.
- Amostragem determinística e deduplicação, com máximo padrão de quatro variantes por exemplo.
- Perturbação apenas no treino, com probabilidade padrão 45% e pesos condicionais 20/20/20/25/15 para swap, omissão, tecla próxima, acento/caixa/pontuação e repetição/espaçamento.
- Mutações são feitas sobre caracteres Unicode e preservam UTF-8 válido.
- Exemplos `sensitive=true` bloqueiam o treino por padrão; o opt-in é explícito na configuração.

### Objetivo e otimização

- Sequência: BOS, USR, pergunta em bytes, BOT, resposta em bytes, EOS.
- Cross-Entropy causal aplicada aos bytes da resposta e EOS. Tokens de prompt não recebem loss auxiliar por padrão; PAD retorna loss zero e gradiente zero.
- Label smoothing 0,05 somente para alvos byte-level comuns; tokens especiais não recebem smoothing.
- BPTT truncado em 128 passos por padrão, configurável até 512. Estado numérico continua entre segmentos; gradiente é destacado na fronteira.
- Dropout variacional padrão 0,10, com máscara fixa dentro de cada segmento e desativado na validação.
- AdamW: LR 1e-3, warmup 3%, cosine decay até 1e-5, beta1 0,9, beta2 0,999, weight decay 1e-3 somente em matrizes e clipping global L2 1,0.
- Weight tying é preservado: gradientes do embedding somam contribuições da entrada e da cabeça de saída, sem matriz duplicada.

### Telemetria e execução

- JSONL append-only com loss por pergunta e por exemplo, loss agregada de treino/validação, normas de gradiente, learning rate, run_id, seed, versão de código/configuração, versão do dataset, example_id e group_id.
- CLI: `morok-g3-train <dataset.jsonl> <metrics.jsonl> <modelo.morok> [epocas] [seed]`; ao terminar, exporta os 100.000 pesos FP32 para o artefato versionado `.morok` (formato 2, arquitetura `0x0302`) por gravação atômica e validação CRC32.
- Biblioteca: `Morok::G3::Training`.
- Testes: `g3.training`, incluindo loader, matriz de expansão, isolamento de grupos, Cross-Entropy, PAD, perturbação, update de pesos e telemetria.

A implementação do pipeline não equivale a um treino de produção. Nenhum corpus real foi incluído ou treinado nesta entrega; a qualidade do modelo depende de dataset com origem, licença e rótulos revisados. O treinador suporta retomada por checkpoint de época, restaurando pesos FP32, momentos AdamW, scheduler, sampler, splits e RNG. A retomada exige configuração, fingerprint do dataset e versão de código compatíveis.
