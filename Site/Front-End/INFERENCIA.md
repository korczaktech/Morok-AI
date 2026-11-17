# Inferência local do Morok Web

## Tsukuyomi G2
O G2 continua sendo executado localmente no navegador por WebAssembly. O frontend baixa o checkpoint específico da pergunta a partir da branch pública `tsukuyomi-state` e o runtime C++ original do G2 carrega esse checkpoint no sistema de arquivos virtual do WebAssembly. A inferência não envia o prompt ao backend.

## Tsukuyomi G3
O G3 também é executado localmente no navegador, sem endpoint remoto de geração.

Fluxo:
1. O frontend carrega `tsukuyomi-g3.js` e o módulo `tsukuyomi-g3.wasm` compilados a partir do adaptador WebAssembly e do backend GRU original da G3.
2. O navegador baixa `wasm/models/g3-tsukuyomi.morok` uma vez e mantém o backend carregado na memória do runtime.
3. O adaptador valida o cabeçalho do artefato G3, dimensões, precisão FP32, tamanho do payload e CRC32 antes de aceitar os pesos.
4. Os 100.000 parâmetros são restaurados nos tensores do `G3Backend`.
5. A pergunta é codificada diretamente como bytes UTF-8 e consumida pela GRU. A geração usa temperatura 0,7, Top-K 10, limite de 256 bytes e seed aleatória por resposta.
6. A saída é devolvida ao frontend e exibida progressivamente. A animação progressiva é apenas apresentação; a inferência é executada localmente pelo WebAssembly.

O artefato publicado é o `g3-rl.morok` do último treinamento G3 concluído com sucesso. O workflow de publicação procura o artefato mais recente não expirado do workflow `treinamento-g3.yml` na branch `main` e o copia para a branch `Website`.

## Limites e compatibilidade
- O runtime web atual carrega artefatos G3 FP32 com 100.000 parâmetros e arquitetura GRU/RMSNorm 0x0302. Não aceita artefatos INT8 nesta primeira integração.
- O estado recorrente é reiniciado para cada mensagem. O histórico da conversa é mantido pela interface, mas não é reaplicado automaticamente como estado neural.
- O adaptador usa os bytes UTF-8 recebidos do navegador diretamente. Ele não aplica a normalização NFC do `ByteTokenizer` nativo que depende de ICU.
- A geração do WebAssembly é síncrona. Enquanto o modelo gera, o navegador pode ficar ocupado; cancelamento durante a chamada C++ não interrompe o kernel.
- A qualidade das respostas depende dos pesos realmente treinados. A integração não implica que o modelo já tenha competência generalista ou qualidade equivalente à G2.
- O arquivo de pesos é baixado para o navegador e a inferência ocorre no dispositivo. A sincronização de conversas autenticadas continua sendo uma operação separada do backend, conforme o fluxo já existente do site.

## Publicação
O workflow `.github/workflows/g3-wasm-site.yml` compila o adaptador com Emscripten, baixa o último artefato de treinamento G3, valida os arquivos e publica o runtime e os pesos diretamente na branch `Website`. A branch `main` é usada somente como fonte de código do backend GRU e dos artefatos de treinamento.
