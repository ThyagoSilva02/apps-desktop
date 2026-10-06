# Distribuição para Windows

O fluxo `OctoKey Windows x64`, em `.github/workflows/octokey-windows.yml` na raiz do repositório `apps-desktop`, compila os fontes da pasta `OctoKey` em uma máquina Windows do GitHub, com MSVC x64 em Release, executa todos os testes do CTest e só então gera o pacote. Ele não reaproveita executáveis enviados junto com o código-fonte. O ZIP contém `MacroPillControl.exe`, instruções de uso e hashes SHA256 dos arquivos. Um segundo `SHA256SUMS.txt`, ao lado do ZIP, permite conferir o arquivo baixado.

O fluxo padrão não aplica assinatura Authenticode. Hospedar no GitHub, compilar no GitHub Actions e fornecer hashes não garantem que todos os antivírus deixarão de exibir avisos.

## Compilar e obter um pacote

O fluxo roda em `push` e `pull_request` que alterem `OctoKey/` ou o próprio workflow, e manualmente em **Actions → OctoKey Windows x64 → Run workflow**. Para uma compilação manual comum, deixe `release_tag` vazio. Depois que os testes passarem, baixe o artefato `MacroPillControl-windows-x64` da execução. Os artefatos ficam disponíveis por 14 dias e podem exigir login no GitHub.

Para empacotar uma compilação local, entre na pasta `OctoKey` do repositório e execute:

```powershell
./scripts/package-windows.ps1 -Version local
```

O script espera `bin/MacroPillControl.exe` já compilado. Para evitar misturar versões, ele recusa uma saída existente; informe uma pasta nova com `-OutputDirectory`. Ele não executa o programa nem instala dependências.

## Disponibilizar um download em Releases

1. Aguarde a compilação e os testes do commit passarem e confira o pacote.
2. Crie uma tag de versão com o prefixo do projeto, como `octokey-v1.0.0`, nesse commit e uma release correspondente no GitHub, inicialmente como rascunho.
3. Execute manualmente o fluxo **OctoKey Windows x64**, selecionando essa tag em **Use workflow from**, e preencha `release_tag` com a mesma tag.
4. O fluxo confere que a tag aponta exatamente para o commit compilado e anexa o ZIP e seu SHA256 à release existente. Ele não cria uma release nem sobrescreve arquivos já anexados.
5. Confira o executável final e os resultados das análises de segurança antes de publicar o rascunho para os usuários.

Uma execução comum ou um pull request nunca publica arquivos em Releases. Somente o trabalho separado de publicação manual recebe permissão de escrita no repositório.

## Assinatura e reputação

Para distribuir um executável com identidade de editor verificável, obtenha uma solução de assinatura de código confiável, como um certificado Authenticode emitido por uma autoridade reconhecida ou um serviço apropriado de assinatura. Assine o executável final com carimbo de tempo e confira a assinatura antes de gerar os hashes e o ZIP. Chaves privadas e credenciais devem permanecer fora do repositório, protegidas pelo serviço de assinatura ou pelos mecanismos de segredos do provedor.

Uma assinatura identifica o editor e permite verificar integridade. Ela não prova ausência de malware, não transforma uma detecção em falso positivo e não garante que SmartScreen ou outros antivírus aceitem uma versão nova. A reputação depende de outros sinais além da assinatura. Certificados autoassinados não estabelecem a confiança pública necessária para usuários que baixam o aplicativo.

## Se aparecer “ameaça detectada”

Registre o nome exato da detecção, o antivírus e sua versão, a versão do programa, o SHA256 do executável e a origem do download. Reproduza com o pacote final, analise o código e as dependências e investigue a detecção antes de classificá-la como falso positivo.

Se a análise indicar falso positivo, encaminhe o arquivo ao canal oficial do fornecedor. Para Microsoft Defender, use [Submit a file for malware analysis](https://www.microsoft.com/en-us/wdsi/filesubmission) como desenvolvedor e descreva o resultado incorreto. A decisão de corrigir a detecção é do fornecedor. Não peça aos usuários para desativar proteção, adicionar exclusões ou ignorar um alerta de ameaça.

“Editor desconhecido” ou aviso de reputação do SmartScreen e “ameaça detectada” pelo antivírus exigem diagnósticos diferentes. Sem o relatório exato, não é possível determinar a causa de um aviso anterior.

## Referências oficiais

- [Microsoft Defender SmartScreen: reputação e assinatura](https://feedback.smartscreen.microsoft.com/smartscreenfaq.aspx)
- [Microsoft: envio de arquivos para análise](https://www.microsoft.com/en-us/wdsi/filesubmission)
- [GitHub Actions: checkout](https://github.com/actions/checkout/releases/tag/v7.0.0)
- [GitHub Actions: upload de artefatos](https://github.com/actions/upload-artifact/releases/tag/v7.0.0)
- [GitHub Actions: download e conferência de artefatos](https://github.com/actions/download-artifact/releases/tag/v8.0.0)

As ações externas do fluxo estão fixadas nos commits completos dessas versões. Atualizações devem ser revisadas antes de mudar os hashes.
