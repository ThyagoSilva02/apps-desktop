MacroPill Control — Windows x64

Requisitos: Windows 10 ou 11, cabo USB de dados e STM32 com firmware
USB serial que envie BTN1 até BTN8. O firmware e as instruções de ligação
ficam na pasta firmware do repositório de código-fonte.

Como usar
1. Extraia o ZIP para uma pasta do seu computador.
2. Conecte a placa por USB e abra MacroPillControl.exe.
3. Escolha a porta COM correspondente à placa e conecte.
4. Configure as oito ações, salve e pressione os botões da placa.

O aplicativo abre somente os programas/arquivos e URLs que você configurar,
e envia os atalhos configurados à janela em primeiro plano quando recebe
comandos da placa ou quando você usa o botão Testar.
A configuração é salva em %LOCALAPPDATA%\MacroPill Control\config.ini.
Use Sair no ícone da bandeja para encerrar o aplicativo.

Integridade e avisos do Windows
SHA256SUMS.txt contém os hashes do executável e destas instruções.
No PowerShell, Get-FileHash .\MacroPillControl.exe -Algorithm SHA256 permite
comparar o hash com o arquivo fornecido. Hash confere integridade;
não representa uma análise de segurança ou assinatura digital.

O fluxo padrão de compilação deste projeto não assina o executável.
Antivírus e SmartScreen podem mostrar avisos; nenhum projeto pode garantir
ausência de alertas em todos os computadores. Se houver detecção de ameaça,
interrompa o uso e informe o nome da detecção, a versão e o hash ao mantenedor.
Mantenha a proteção do Windows habilitada.
