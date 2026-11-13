# Etapa 1 — Infraestrutura inicial

Status: arquivos implementados; execução local e CI ainda precisam confirmar os critérios.

## Entregas
- CMake 3.25+ e C++20.
- Presets Ninja para Debug, Release e ASan+UBSan.
- Opções de avisos GCC/Clang/MSVC.
- Sanitizers opcionais para GCC/Clang.
- Biblioteca estática inicial Morok::Core.
- API mínima de versão do build.
- Teste automatizado via CTest.
- GitHub Actions para GCC e Clang em Debug e Release.
- .gitignore e .clang-format.
- Estrutura de diretórios inicial, expansível até G30.

Nenhuma dependência ML, GPU ou biblioteca externa é exigida nesta etapa. SQLite será integrado na etapa de persistência.

## Comandos
```bash
cmake --preset debug
cmake --build --preset debug --parallel 2
ctest --preset debug
cmake --preset release
cmake --build --preset release --parallel 2
ctest --preset release
cmake --preset asan
cmake --build --preset asan --parallel 2
ctest --preset asan
```

## Critérios de aceite
- [ ] Configuração GCC 13+ e Clang 17+.
- [ ] Build Debug e Release.
- [ ] CTest passa em Debug e Release.
- [ ] ASan+UBSan passa em ambiente compatível.
- [ ] CI conclui GCC/Clang × Debug/Release.
- [ ] Nenhuma dependência ML/GPU foi introduzida.

A existência dos arquivos não comprova compilação. Marcar critérios somente após execução real.
