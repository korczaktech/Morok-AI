# 18. Especificação operacional fechada da G1

**Versão:** 1.0  
**Estado:** decisões de implementação aprovadas; valores experimentais iniciais, não resultados medidos.  
**Escopo:** fechar contratos que estavam implícitos nos documentos 09–17. Este documento prevalece quando detalha um padrão operacional ainda não definido.

## 18.1 Perfil de execução e limites de recursos

Hardware de referência: máquina com 4 GB de RAM física.

- Meta de memória residente do processo: até 512 MiB.
- Limite rígido inicial do processo: 1 GiB. Ao atingir 90% desse limite, interromper a expansão de buffers e reduzir trabalho opcional; se não for possível recuperar margem, encerrar o experimento com erro de memória controlado.
- Reservar no mínimo 2 GiB para sistema operacional e demais processos. O processo não deve tentar ocupar toda a RAM física.
- Precisão inicial: FP32 para parâmetros, estados, gradientes e cálculo da perda.
- Corpus carregado em fluxo; não carregar o corpus inteiro na RAM.
- Bloco de leitura do corpus: 64 KiB.
- Blocos BPTT testados inicialmente: 32, 64 e 128 bytes; padrão inicial B=64.
- Lote inicial: uma sequência/documento por fluxo recorrente. Micro-lotes só serão habilitados após medir RAM e confirmar semântica correta de estado.
- Paralelismo inicial: um único worker de treinamento e um worker de carregamento no máximo. Paralelismo adicional exige benchmark.
- Toda alocação de cache deve ter limite explícito. Falha de alocação não pode ser ignorada.

Os limites são políticas do processo, não promessa de que a implementação já os respeita.

## 18.2 Contrato causal de predição

A indexação fica definida sem ambiguidade:

1. O byte observado na posição i é convertido em x_i e processado pelo núcleo.
2. O núcleo atualiza h_f(i) e h_s(i).
3. A cabeça usa esses estados para prever o byte da posição i+1.
4. A perda compara a previsão com o byte real i+1.
5. O primeiro byte de cada sequência é contexto observado; não há previsão anterior dentro daquela sequência.
6. A última posição não gera alvo se não houver próximo byte.

Portanto, a previsão não pode usar o byte-alvo nem bytes posteriores. O teste causal deverá alterar bytes futuros e verificar que logits anteriores permanecem idênticos.

A entrada do núcleo é o vetor fixo de **11 características** definido no documento 13: oito bits, paridade, contagem normalizada de bits 1 e valor normalizado do byte. Qualquer referência a vetor de oito dimensões no protocolo de treinamento significa somente os oito bits-alvo da cabeça, não a entrada do núcleo.

## 18.3 Política de UTF-8 e dados

O modo padrão da G1 é texto UTF-8 estrito.

- Na ingestão de corpus textual, validar UTF-8 antes de treinar.
- Sequência inválida: rejeitar o documento inteiro, registrar identificador, posição do erro e motivo; nunca remover bytes inválidos silenciosamente.
- Não substituir automaticamente por U+FFFD, pois isso altera o corpus e mascara erros.
- Um modo binário bruto poderá ser implementado futuramente, mas não será misturado com métricas de texto UTF-8.
- A unidade de treinamento e contagem é o byte, não o caractere Unicode.
- A geração mantém bytes em buffer até formar uma sequência UTF-8 válida. Não apresentar fragmentos incompletos como texto.
- Se o limite de geração terminar no meio de um caractere, descartar apenas o sufixo incompleto da resposta apresentada e registrar que o limite encerrou a sequência; preservar os bytes brutos no resultado técnico.
- A validação de entrada e saída deve rejeitar sobrelongas, surrogates codificados e valores acima de U+10FFFF, conforme UTF-8 estrito.

## 18.4 Divisão, limpeza e prevenção de vazamento

- Dividir por documento ou fonte antes de criar janelas ou blocos.
- Padrão inicial: 90% treino, 5% validação e 5% teste, por documento, com semente fixa.
- Remover duplicatas exatas antes da divisão.
- Verificar duplicatas entre divisões por hash de conteúdo; se houver, manter apenas uma ocorrência na divisão de maior prioridade: teste, validação, treino.
- Não mover fragmentos de um mesmo documento para divisões diferentes.
- Deduplicação aproximada será registrada como experimento separado; não aplicar heurística destrutiva sem relatório.
- Teste final fica bloqueado para escolha de hiperparâmetros. Usar validação para decisões.
- Cada corpus exige origem, licença, idioma, domínio, tamanho em bytes, número de documentos, hash, regras de limpeza e divisão.

## 18.5 Hiperparâmetros iniciais congelados para o primeiro baseline

Configuração de referência B0:

| Item | Padrão inicial |
|---|---:|
| Precisão | FP32 |
| Dimensão do estado | 11 |
| Comprimento BPTT | 64 bytes |
| Otimizador principal | AdamW |
| Learning rate | 0,001 |
| beta1 | 0,9 |
| beta2 | 0,999 |
| epsilon | 1e-8 |
| weight decay | 0,0001 |
| Clipping da norma global | 1,0 |
| lambda_group | 0,05 |
| Semente padrão de depuração | 42 |
| Inicialização | Xavier uniform nas matrizes de entrada e cabeça; ortogonal nas matrizes recorrentes |
| Viés do gate rápido | 0,0 |
| Viés do gate lento | +1,0 |
| Viés da cabeça de predição | 0,0 |
| Viés inicial do pontuador | 0,0 |

AdamW é o padrão de qualidade para B0. SGD com momentum (learning rate 0,01; momentum 0,9; weight decay 0,0001) é baseline comparativo, não deve atualizar o mesmo experimento em paralelo.

Não ajustar vários hiperparâmetros simultaneamente. Primeiro comparar BPTT 32/64/128 mantendo os demais valores fixos; depois fazer busca controlada de learning rate em {0,0003; 0,001; 0,003}. A validação escolhe o melhor. O teste final não participa.

O bias positivo do gate lento favorece retenção inicial maior; não é garantia de memória longa. Registrar saturação dos gates e gradientes.

## 18.6 Regras de atualização e parada

- Atualizar parâmetros após cada bloco BPTT com pelo menos um alvo válido.
- Clipping pela norma global dos gradientes, antes do passo do otimizador.
- Não aplicar weight decay aos vieses; aplicar às matrizes e pesos, com grupos de parâmetros explícitos.
- Não aplicar atualização se a perda ou qualquer gradiente contiver NaN/Inf.
- Interromper se a norma global pré-clipping exceder 100 por 10 atualizações consecutivas, ou se permanecer exatamente zero por 100 atualizações com alvos válidos.
- Avaliar no conjunto de validação ao fim de cada época.
- Early stopping: interromper após 5 avaliações consecutivas sem melhora de BPB; restaurar checkpoint com melhor BPB de validação.
- Limite inicial: 50 épocas. É um teto operacional, não previsão de convergência.
- Salvar checkpoint ao melhorar BPB e também a cada 5 épocas para recuperação.

## 18.7 Segmentação: limites numéricos iniciais

A segmentação G1 é um subsistema experimental auxiliar. O núcleo de predição principal continua consumindo bytes em ordem causal. Na primeira implementação, a hierarquia **não substitui bytes por tokens no caminho do núcleo**. Assim, não atribuir à segmentação redução de latência ou melhoria de BPB end-to-end sem uma variante que realmente alimente a representação agrupada ao preditor.

Padrões iniciais:

- Nível 0: bytes.
- Nível 1: sequências contíguas de 2 a 8 bytes.
- Nível 2: concatenação de exatamente 2 a 4 unidades elegíveis de nível 1; comprimento total máximo de 32 bytes.
- Frequência mínima: 4 ocorrências no bloco de avaliação.
- Máximo de 256 candidatos únicos avaliados por bloco.
- Máximo de 64 candidatos ativos por versão da segmentação.
- Máximo de 4.096 entradas no cache de contagens; política de remoção LRU entre blocos.
- Hash de 64 bits pode indexar candidatos, mas igualdade dos bytes deve ser confirmada para evitar colisões.
- Atualização de segmentação: ao fim de cada época, nunca no meio de um bloco BPTT.
- Uma alteração só é aceita se o round-trip do corpus de validação amostrado for exato e o custo total de metadados estiver incluído.

Como o caminho preditivo B0 permanece byte a byte, P(g) será medido inicialmente como **associação preditiva diagnóstica**: redução da entropia empírica do próximo byte em ocorrências do candidato, com suavização, comparada à distribuição-base do mesmo bloco. Não chamar isso de ganho causal do modelo nem de redução de BPB end-to-end. Uma futura variante que consuma unidades agrupadas deverá ter avaliação separada.

## 18.8 Metas e controlador adaptativo

A versão inicial usa metas operacionais, sujeitas a calibração em validação:

- frequência: F(g) deve satisfazer o mínimo de quatro ocorrências;
- ganho preditivo diagnóstico: meta P*=0,02;
- benefício líquido de compressão: meta C*=0,05;
- preservação: R*=1 e é condição rígida.

Para os três critérios graduais F, P e C, definir déficit:
d_i = clip((target_i - observed_i) / max(target_i, 1e-6), -1, 1).

F usa a frequência normalizada observada e target correspondente ao mínimo de frequência normalizado no bloco. P e C usam suas metas acima. Para R, não atualizar peso: candidatos com R=0 são rejeitados antes do controlador; o controlador registra falha de integridade como evento crítico.

Controlador:
- eta = 0,05;
- w_min = 0,05;
- w_max = 0,60;
- teto de frequência = 0,20;
- pesos iniciais: F=0,10; P=0,40; C=0,25; R=0,25.

Atualização ocorre somente depois de uma avaliação completa e comparável. Se não houver amostra suficiente, se uma métrica não puder ser calculada ou se o conjunto de validação mudar, manter os pesos anteriores. Aplicar atualização multiplicativa aos critérios graduais, projetar nos limites, aplicar o teto de frequência e renormalizar respeitando os limites. O peso R permanece fixo em 0,25 na G1; os pesos F/P/C são renormalizados para somar 0,75. Essa regra mantém a preservação explícita e evita que o controlador tente compensar falhas de round-trip.

Limiar inicial de criação U_create=0,60 e de retenção U_keep=0,50. Histerese: candidato ativo só é removido quando U<0,50; candidato novo só entra quando U>=0,60. Ambos são parâmetros de configuração registrados e calibráveis em validação.

## 18.9 Definição operacional de compressão

A métrica C considera o custo real de representar o bloco:
- bytes originais;
- tabela/dicionário de unidades;
- identificadores das unidades;
- comprimentos;
- escapes para bytes/unidades não cadastradas;
- cabeçalhos e versão do formato;
- alinhamento e metadados.

Não contar somente o tamanho da sequência de índices. Se o formato agrupado não reduzir o total de bytes armazenados/transmitidos, C=0. A comparação usa o mesmo bloco e o mesmo método de serialização, e o formato deve permitir reconstrução byte a byte.

## 18.10 Geração e API de inferência

Padrão de geração da G1:
- estratégia gulosa determinística;
- bit igual a 1 quando logit >= 0; caso contrário 0;
- ordem de bits: mais significativo para menos significativo;
- máximo configurável de 256 bytes por chamada, como limite inicial;
- estado fast/slow isolado por sessão;
- estado reinicializado em zero para nova sessão;
- não há token EOS dedicado na G1; a parada ocorre pelo limite configurado ou por sequência de parada fornecida pelo chamador;
- amostragem só após baseline estável: logit dividido por temperatura T, com T inicial de teste 0,8; gerador pseudoaleatório com semente explícita;
- sem top-p, penalidade de repetição ou heurísticas de chat na G1.

A API deverá distinguir bytes gerados de texto validado. Deve retornar status explícito quando o limite encerrar uma sequência UTF-8 incompleta.

## 18.11 Checkpoint e retomada

Formato binário versionado, little-endian, com:
- magic bytes e versão do formato;
- versão da arquitetura e identificador do inventário;
- dimensão e tipos numéricos;
- parâmetros em ordem canônica;
- estados do otimizador quando retomar treinamento;
- contador de época, bloco e atualização;
- estado RNG;
- estado fast/slow apenas se retomada de sequência for explicitamente solicitada;
- configuração de treinamento e versão da segmentação;
- hash/checksum do payload.

Carregamento deve validar magic, versão, dimensões, tamanho esperado, checksum e finitude de todos os valores antes de modificar o modelo em memória. Salvar em arquivo temporário e renomear atomicamente após gravação completa. Checkpoint incompatível deve ser rejeitado com diagnóstico.

## 18.12 Contratos de sessão, concorrência e segurança

- Cada sessão possui seus próprios estados fast/slow e RNG.
- Nunca compartilhar estado recorrente entre sessões.
- Parâmetros do modelo são somente leitura durante inferência.
- Atualização de pesos exige modelo fora de serviço ou troca atômica de snapshot; nunca modificar matrizes enquanto uma inferência as lê.
- O primeiro servidor poderá ser single-process/single-worker; concorrência não é requisito da G1.
- Limitar bytes de entrada por chamada e validar tamanho antes de alocar.
- Não executar conteúdo produzido pelo modelo como código ou comando.
- Logs não devem armazenar texto sensível por padrão; registrar métricas e identificadores de experimento.

## 18.13 Testes de aceitação da especificação

Antes de qualquer treino de referência, a implementação deve passar por:

1. Teste de inventário: exatamente 1.000 parâmetros treináveis.
2. Teste de formas: todas as matrizes/vetores correspondem às dimensões documentadas.
3. Teste de causalidade: modificar futuro não altera logits passados.
4. Teste de fatoração: probabilidade conjunta consistente com produto das condicionais e soma das 256 probabilidades de byte aproximadamente 1.
5. Teste numérico: BCE-with-logits finita para logits extremos.
6. Teste de gradiente: comparação por diferenças finitas em parâmetros selecionados.
7. Teste de estado: sequência em blocos com estado transportado equivale ao processamento contínuo no forward.
8. Teste de truncamento: estado é transportado, mas gradiente não atravessa fronteira BPTT.
9. Teste UTF-8: entradas válidas aceitas e inválidas rejeitadas com posição.
10. Teste round-trip: qualquer agrupamento aceito reconstrói bytes idênticos.
11. Teste de colisão: hash igual com bytes diferentes não funde candidatos.
12. Teste de cache: limite máximo respeitado e remoção determinística.
13. Teste de controlador: pesos dentro dos limites, frequência <=0,20, soma total 1 e peso R=0,25.
14. Teste de checkpoint: salvar/carregar preserva logits; arquivo truncado ou corrompido é rejeitado.
15. Teste de isolamento: sessões distintas não compartilham estado.
16. Teste de limite: memória, tamanho de entrada e máximo de geração respeitados.

## 18.14 Critério para considerar as especificações G1 encerradas

A especificação G1 estará documentalmente encerrada quando:
- cada tensor treinável tiver forma, inicialização e papel definidos;
- fluxo causal e indexação de alvo estiverem definidos;
- dados, UTF-8, divisão e deduplicação estiverem definidos;
- treinamento, otimização, parada e retomada tiverem padrões;
- segmentação tiver limites, formato reversível e papel no caminho de predição explicitado;
- inferência, geração, sessões e erros tiverem contratos;
- recursos, limites e testes de aceitação estiverem definidos.

O encerramento documental não significa implementação, teste ou validação científica. Esses estados só podem ser atribuídos após execução real.

## 18.15 Precisões matemáticas adicionais

### Codificação exata das 11 características

Para byte b em [0,255], os oito bits são extraídos do mais significativo ao menos significativo e convertidos para {-1,+1}: bit 1 vira +1; bit 0 vira -1. As três características restantes são:
- paridade: +1 quando popcount(b) é par; -1 quando é ímpar;
- contagem: popcount(b)/4 - 1, no intervalo [-1,1];
- valor: b/127,5 - 1, no intervalo [-1,1].

### BPB e redução da perda

Se BCE-with-logits retornar a média sobre bytes e oito bits, então:
BPB = 8 * BCE_mean / ln(2), quando BCE está em nats.
Se a implementação acumular a soma das oito perdas por byte, BPB = soma_nats / (N_bytes * ln(2)).
Os relatórios devem registrar qual redução foi usada. Não misturar as duas convenções.

### Gradientes

A G1 usará retropropagação manual (reverse-mode) específica para as operações do núcleo e da cabeça, evitando dependência de framework de autodiferenciação. Implementar derivadas de affine, sigmoid, tanh, soma, produto elemento a elemento e mistura de estados; aplicar BPTT dentro do bloco B. A cabeça triangular será retropropagada em ordem reversa dos bits, acumulando gradientes nos 28 coeficientes A e nos logits. Os gradientes deverão ser comparados por diferenças finitas centrais em parâmetros selecionados e em modelos minúsculos determinísticos.

### Associação preditiva diagnóstica P(g)

Para cada candidato elegível, reunir os bytes imediatamente seguintes às ocorrências de g, respeitando limites de documento. Estimar a distribuição global q0 e a distribuição condicionada qg sobre 256 valores, com suavização add-one. Calcular entropias empíricas H(q0) e H(qg), em bits:
P(g) = clip((H(q0) - H(qg)) / max(H(q0), epsilon), 0, 1).
Exigir pelo menos quatro ocorrências. Essa métrica descreve associação estatística no corpus; não mede melhoria causal do modelo B0.

### Projeção dos pesos

O peso R permanece 0,25. Os pesos F, P e C são atualizados multiplicativamente e projetados no conjunto com soma 0,75, limites individuais [0,05;0,60] e teto adicional F<=0,20. A projeção usa water-filling determinístico: aplicar um deslocamento comum aos três pesos, saturar limites, redistribuir o restante entre os não saturados até somar 0,75. Se não houver solução numérica válida, manter os pesos anteriores.

### Pontuador: características não redundantes

R é condição rígida e não entra no vetor treinável: todos os exemplos aceitos têm R=1. Para manter 22 características informativas, usar:
- F, P e C: 3;
- médias dos oito bits do candidato: 8;
- oito coordenadas do estado rápido: 8;
- comprimento normalizado: 1;
- diversidade interna (número de valores de byte distintos dividido pelo comprimento): 1;
- sobreposição interna de prefixo/sufixo mais longa normalizada: 1.
Total: 22. Frequência é representada somente por F; não duplicar com outra feature de frequência. R continua verificada antes do pontuador.

### Atualização do pontuador

A cada avaliação de segmentação, treinar o pontuador por uma passagem sobre os candidatos rotulados, em um passo AdamW com learning rate 0,001 e BCE-with-logits. Separar os estados do otimizador do preditor. Se houver menos de 16 candidatos rotulados, não atualizar. Comparar a pontuação aprendida com a ordenação determinística U(g); se não aproximar o critério ou piorar estabilidade, desativar o pontuador na configuração experimental sem alterar o inventário oficial da arquitetura G1.

## 18.16 Inicialização completa e retropropagação

Inicialização determinística:
- matrizes de entrada W_ax, W_cx: Xavier uniform;
- matrizes recorrentes W_ah, W_ch, W_gh, W_gs: ortogonal;
- matriz O da cabeça: Xavier uniform;
- matriz triangular A: zeros;
- vieses da cabeça: zeros;
- pesos v e vieses θ do pontuador: zeros;
- estados h_f e h_s: zeros por sequência nova.

Derivadas locais para o estado rápido, com h'=a⊙h_prev+(1-a)⊙c:
- δa = δh'⊙(h_prev-c);
- δh_prev,direto = δh'⊙a;
- δc = δh'⊙(1-a);
- δz_a = δa⊙a⊙(1-a);
- δz_c = δc⊙(1-c⊙c).

Para o estado lento, com s'=g⊙s_prev+(1-g)⊙h:
- δg = δs'⊙(s_prev-h);
- δs_prev,direto = δs'⊙g;
- δh,direto = δs'⊙(1-g);
- δz_g = δg⊙g⊙(1-g).

Os gradientes dos estados rápido e lento devem ser somados antes de retropropagar pelo estado rápido. Gradientes das matrizes são produtos externos entre gradiente pré-ativação e entrada correspondente; gradientes dos vieses são somas no tempo. A BPTT percorre os bytes do bloco em ordem inversa. Na fronteira do bloco, manter h_f/h_s numéricos e zerar os gradientes associados ao grafo anterior.

Na cabeça, para cada bit j, δz_j = p_j-y_j. Acumular gradientes em O_j, c_j e A_{j,k}; os bits-alvo anteriores são entradas observadas constantes, sem gradiente. A implementação deverá validar essas regras por diferenças finitas centrais em pelo menos um peso de cada matriz, um viés de gate e um coeficiente A.

## 18.17 Formato binário RLAH-BLOCK v1

O formato comprimido por bloco é determinístico e reversível. Inteiros são little-endian.

Cabeçalho fixo de 24 bytes:
- magic: quatro bytes ASCII `RLAH`;
- version: u16 = 1;
- flags: u16; bit 0 indica payload com dicionário; demais bits devem ser zero;
- raw_length: u32, bytes originais;
- dictionary_count: u16, de 0 a 64;
- reserved: u16 = 0;
- token_count: u32;
- raw_crc32: u32, CRC-32/IEEE dos bytes originais.

Após o cabeçalho:
- dicionário: para cada entrada, um comprimento u8 seguido dos bytes da unidade; comprimento entre 2 e 32;
- fluxo de tokens: exatamente token_count tokens. Tag 0 é seguida por um byte literal. Tag 1 é seguida por um ID u8 entre 0 e dictionary_count-1.

Quando flags=0, dictionary_count=0, token_count=raw_length e o payload contém exatamente os bytes originais, sem tokens. Quando flags=1, o fluxo usa tags e referências do dicionário.

Codificação:
1. escolher no máximo 64 unidades ativas;
2. ordenar IDs por score decrescente e, em empate, bytes lexicográficos crescentes;
3. percorrer o bloco da esquerda para a direita;
4. selecionar a unidade correspondente mais longa no offset atual;
5. em empate de comprimento, usar maior score; persistindo empate, ordem lexicográfica;
6. se nenhuma unidade corresponder, emitir literal;
7. se a representação completa não for menor que o payload bruto, usar flags=0.

Decodificação:
- validar magic, versão, flags, reserved, contagens e limites antes de alocar;
- validar comprimento e bytes de cada entrada do dicionário;
- validar cada tag e ID;
- impedir que a saída exceda raw_length;
- exigir comprimento final exatamente igual a raw_length;
- validar CRC32;
- rejeitar bytes restantes, truncamento ou campos inconsistentes.

O custo de compressão C usa o tamanho integral serializado, incluindo cabeçalho, dicionário e tokens. O bloco de segmentação tem 4.096 bytes no padrão inicial; o tamanho pode ser menor no final do documento.

## 18.18 Formato binário de checkpoint MOROKCKP v1

Todos os inteiros são little-endian. Cabeçalho fixo:
- magic: oito bytes ASCII `MOROKCKP`;
- format_version: u16 = 1;
- header_size: u16;
- flags: u32; bit 0 indica estados AdamW presentes; bit 1 indica estado RNG presente; bit 2 indica cursor de treinamento presente;
- architecture_id: 32 bytes identificando RLAH-G1-fastslow-d11-bit8;
- parameter_count: u32, deve ser 1000;
- dtype: u8 = 1 (FP32);
- reserved: três bytes zero;
- optimizer_step: u64;
- epoch: u32;
- block_index: u64;
- parameter_payload_bytes: u64, deve ser 4000;
- optimizer_payload_bytes: u64;
- rng_payload_bytes: u32;
- metadata_payload_bytes: u32;
- payload_crc32: u32, CRC-32/IEEE de todos os payloads concatenados.

Payload em ordem:
1. 1.000 parâmetros FP32 na ordem canônica do inventário;
2. se flag AdamW: 1.000 valores FP32 de primeiro momento e 1.000 de segundo momento;
3. se flag RNG: quatro u64 de estado do gerador xoshiro256**; estado totalmente zero é inválido;
4. se flag cursor: cursor de documento, offset de byte e estado de embaralhamento serializados em campos de tamanho fixo;
5. metadados UTF-8 com tamanho limitado a 64 KiB, contendo configuração, hash do corpus, commit, compilador e versão.

O cabeçalho deve declarar comprimentos exatos. Rejeitar arquivo truncado, bytes extras, CRC inválido, parâmetro não finito, arquitetura divergente ou dimensão inesperada. Gravar em arquivo temporário, sincronizar e renomear atomicamente. Checkpoint de inferência pode omitir otimizador e cursor, mas nunca parâmetros, versão, contagem e checksum.

O gerador xoshiro256** será inicializado por SplitMix64 a partir da semente explícita. Não usar estado RNG implícito global. A mesma versão do gerador e a mesma semente devem produzir a mesma sequência.

## 18.19 Leitura e embaralhamento de dados

A divisão treino/validação/teste é feita por documento antes de gerar blocos. A ordem dos documentos de treino é embaralhada por época usando o gerador especificado; os bytes dentro de cada documento nunca são embaralhados. A lista de IDs e o estado RNG são persistidos para retomada determinística.

O estado recorrente é zerado no início de cada documento. Blocos BPTT podem atravessar fronteiras de buffer de leitura, mas não fronteiras de documentos. Nunca transportar estado entre documentos distintos.

## 18.20 Ordem canônica dos 1.000 parâmetros

A serialização, inventário e carregamento usam esta ordem exata, row-major para matrizes:

1. W_ax: 11×11 = 121;
2. W_ah: 11×11 = 121;
3. W_cx: 11×11 = 121;
4. W_ch: 11×11 = 121;
5. b_a: 11;
6. b_c: 11;
7. W_gh: 11×11 = 121;
8. W_gs: 11×11 = 121;
9. b_g: 11;
10. O: 8×22 = 176;
11. c: 8;
12. A: triangular estrita por linhas, j=1..7 e k=0..j-1 = 28;
13. v: 22;
14. θ_len: sete valores na ordem de comprimentos 2,3,4,5,6,7,8.

Soma: 506 + 253 + 212 + 29 = 1.000. O carregador deverá verificar cada segmento e o total antes de copiar qualquer parâmetro. architecture_id é a string ASCII `RLAH-G1-fastslow-d11-bit8`, completada com bytes NUL até 32 bytes. FP32 é IEEE-754 binary32.

## 18.21 Precisões dos formatos e retomada

- MOROKCKP v1 possui header_size=104 bytes.
- CRC-32/IEEE usa polinômio refletido 0xEDB88320, inicialização 0xFFFFFFFF e XOR final 0xFFFFFFFF.
- AdamW armazena exatamente 8.000 bytes de momentos (1.000 FP32 de primeiro momento + 1.000 FP32 de segundo momento). SGD com momentum armazena 4.000 bytes. optimizer_id e hiperparâmetros ficam nos metadados.
- Se a flag de cursor estiver ativa, o início do payload de metadados contém document_count u32, seguido de document_count índices u32 na ordem embaralhada, current_document_position u32 e byte_offset u64. O restante do payload é o bloco de metadados UTF-8. Sem cursor, o payload contém apenas metadados.
- O estado RNG xoshiro256** contém exatamente quatro u64. SplitMix64 expande a semente de 64 bits em estado inicial não nulo.
- Para salvar checkpoint de inferência, omitir momentos, RNG e cursor; manter header, parâmetros e metadados.
- Um checkpoint de treinamento só é retomável se optimizer_id, hiperparâmetros, corpus hash, permutação dos documentos, cursor e estado RNG estiverem presentes e forem compatíveis.
