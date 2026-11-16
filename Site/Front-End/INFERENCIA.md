# Inferência do Tsukuyomi G2 no Morok Web

## Fluxo

1. O frontend recebe a mensagem.
2. O módulo WebAssembly executa o código C++ original do Tsukuyomi G2 no navegador.
3. O runtime calcula o hash FNV-1a da pergunta UTF-8 e seleciona o checkpoint individual correspondente.
4. O frontend recebe a resposta e atualiza a conversa.
5. Para contas autenticadas, o frontend envia a conversa completa a `POST /v1/conversations/sync`; o backend Render valida a sessão e persiste mensagens e conversa no MongoDB Atlas.

A inferência não envia prompts ao backend. O backend continua responsável por autenticação, autorização e persistência.

## Artefatos

O workflow `.github/workflows/Pagina.yml` compila `wasm_api.cpp` junto ao código oficial de G2 presente na branch `main`, baixa os 40 checkpoints da branch `tsukuyomi-state` e publica `tsukuyomi.js`, `tsukuyomi.wasm` e `tsukuyomi.data` como parte do site.

O workflow atualiza a cópia publicada dos pesos a cada seis horas, além de cada publicação da branch `Website` e de execuções manuais.

## Limitação atual do modelo

O Tsukuyomi G2 0.1 possui 40 checkpoints especializados, cada um associado ao texto exato de uma pergunta do corpus. Ele não é um modelo generalista e não consegue responder de forma confiável a perguntas inéditas. O frontend informa quando não existe checkpoint para a pergunta em vez de inventar uma resposta.

## Privacidade

O código de inferência e os pesos são baixados para o navegador. O conteúdo da mensagem só é enviado ao backend quando o frontend sincroniza a conversa de uma conta autenticada, para persistência no MongoDB. Usuários sem sessão mantêm o histórico apenas no armazenamento local do navegador.
