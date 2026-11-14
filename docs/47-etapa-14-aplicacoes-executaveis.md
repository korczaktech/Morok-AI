# Etapa 14 — Aplicações executáveis

**Sequência:** roteiro de 20 etapas do projeto. Esta etapa trata da camada de aplicações executáveis; não deve ser confundida com a numeração das etapas operacionais internas da G1.

## Objetivo

Entregar pontos de entrada utilizáveis para preparar corpus, treinar, inspecionar checkpoints, avaliar modelos, gerar texto e servir inferência local. As aplicações devem reutilizar os contratos e subsistemas do núcleo, impor limites de entrada e documentar claramente suas fronteiras de segurança.

## Aplicações

| Executável | Função |
|---|---|
| \`morok-cli\` | Interface unificada: versão, inspeção de checkpoint e geração por prompt |
| \`morok-convert\` | Preparação de corpus: percorre arquivos \`.txt\`, valida UTF-8 estrito e copia a estrutura para uma pasta de saída |
| \`morok-train\` | Treinamento G1: épocas, semente, retomada, checkpoint, métricas JSONL e exportação do melhor modelo |
| \`morok-evaluate\` | Avaliação de checkpoint sobre todos os documentos \`.txt\` de um corpus, com BPB e perplexidade por byte |
| \`morok_benchmark\` | Experimentos comparativos reproduzíveis, baseline e métricas de desempenho |
| \`morok_generate\` | Geração offline a partir de checkpoint e arquivo de prompt |
| \`morok_complete\` | Fluxo legado de demonstração \`entrada.txt\` → \`saida.txt\` |
| \`morok-server\` | Servidor HTTP local para conversação com G1 |

## Contratos de execução

- C++20, CMake 3.25+ e Linux x86-64 como plataforma inicial documentada.
- Todas as aplicações são registradas no build CMake e instaláveis em \`bin\`.
- \`MOROK_BUILD_APPS=OFF\` desativa os novos alvos de aplicações da camada desta etapa.
- Arquivos de prompt passam por validação UTF-8 estrita e limites de tamanho.
- O avaliador reinicia o estado recorrente entre documentos e calcula cada alvo usando somente os bytes anteriores daquele documento.
- O conversor não segue links simbólicos, limita o tamanho por arquivo e o total do corpus e não sobrescreve uma pasta de destino existente.
- O servidor vincula exclusivamente a \`127.0.0.1\`, limita cabeçalhos e corpo HTTP, aceita somente \`Content-Length\` e rejeita \`Transfer-Encoding\`.
- O servidor expõe \`GET /health\` e \`POST /v1/chat\` (mantendo \`/v1/generate\` como alias de compatibilidade). O corpo JSON contém \`prompt\` e, opcionalmente, \`max_new_bytes\`.
- O servidor usa IA-CORE para inicializar e encerrar IA-LANG, IA-MEM, IA-REAS, IA-TOOLS, IA-COMM, IA-LEARN e o runtime. Cada conversa recebe sessão e RequestId próprios.
- Os sinks de auditoria do servidor emitem metadados de eventos no stderr; não registram conteúdo do prompt nem resposta.
- O servidor é sequencial e destinado a desenvolvimento local. Não oferece TLS, autenticação, autorização de rede, concorrência de requisições nem isolamento por processo. Não deve ser exposto à Internet.

## Uso

\`\`\`sh
cmake --preset release
cmake --build --preset release
ctest --preset release

morok-cli --help
morok-cli version
morok-cli inspect artifacts/checkpoints/g1.morok
morok-cli generate artifacts/checkpoints/g1.morok prompt.txt resposta.txt 128

morok-convert data/raw data/processed
morok-train data/processed 50 42 --checkpoint artifacts/checkpoints/g1.morok
morok-evaluate artifacts/checkpoints/g1.morok data/processed
morok_benchmark data/processed artifacts/reports/benchmark.jsonl 3 42 "CC-BY" "corpus-local" 1

morok-server artifacts/checkpoints/g1.morok 8080
\`\`\`

Exemplo de requisição local:

\`\`\`sh
curl http://127.0.0.1:8080/health
curl -X POST http://127.0.0.1:8080/v1/chat \\
  -H 'Content-Type: application/json' \\
  -d '{"prompt":"Olá, Morok!","max_new_bytes":64}'
\`\`\`

## Testes

O CTest registra testes de fumaça para ajuda, versão e rejeição de comandos desconhecidos do CLI. Os componentes de modelo, checkpoint, geração, memória, comunicação, segurança e ferramentas mantêm seus próprios testes unitários.

A validação completa de integração de ponta a ponta ainda depende de executar o pipeline de CI após as alterações desta etapa. Um teste de compilação não comprova qualidade linguística da geração.

## Limitações explícitas

- A G1 é um protótipo experimental de 1.000 parâmetros. Executar uma aplicação não significa que o modelo tenha qualidade conversacional útil.
- A avaliação calcula a perda byte a byte sobre o corpus informado; o operador é responsável por usar um conjunto de teste separado e licenciado. O executável não faz uma nova divisão de dados.
- IA-MEM é inicializado pelo servidor, mas a recuperação automática de memórias não está ligada ao prompt da conversa nesta etapa.
- IA-REAS, IA-TOOLS e IA-LEARN são inicializados pelo composition root do servidor, mas não são acionados automaticamente pelo endpoint de chat.
- O servidor local não é um serviço de produção. Para implantação pública são necessários TLS, autenticação, rate limiting, concorrência controlada, logs operacionais e sandbox de ferramentas.
- O servidor não faz treinamento nem atualiza checkpoints em execução. O modelo é carregado na inicialização.

## Critério de conclusão

Aplicações de CLI, conversão de corpus, treinamento, avaliação, benchmark, geração offline e servidor local possuem alvos CMake, contratos documentados, limites básicos e testes de fumaça. A etapa só deve ser marcada como validada após CI Debug/Release e Sanitizers concluírem com sucesso.
