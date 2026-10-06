param([string]$ToolchainDirectory = '')
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$compiler = if ($ToolchainDirectory) { Join-Path $ToolchainDirectory 'arm-none-eabi-gcc.exe' } else { 'arm-none-eabi-gcc.exe' }
$objcopy = if ($ToolchainDirectory) { Join-Path $ToolchainDirectory 'arm-none-eabi-objcopy.exe' } else { 'arm-none-eabi-objcopy.exe' }
$vendor = Join-Path $root 'third_party\libopencm3'
$build = Join-Path $root 'build'
New-Item -ItemType Directory -Force -Path $build | Out-Null
$flags = @('-mcpu=cortex-m3','-mthumb','-Os','-std=c11','-ffreestanding','-fno-builtin',
    '-ffunction-sections','-fdata-sections','-fno-unwind-tables','-fno-asynchronous-unwind-tables',
    '-DSTM32F1','-DNDEBUG','-Wall','-Wextra',('-I'+(Join-Path $root 'freestanding')),('-I'+(Join-Path $vendor 'include')))
$sources = @('main.c','startup.c','runtime.c') | ForEach-Object { Join-Path $root $_ }
$librarySources = @('stm32/f1/rcc.c','stm32/f1/gpio.c','stm32/common/rcc_common_all.c',
    'stm32/common/gpio_common_all.c','stm32/f1/flash.c','stm32/common/flash_common_all.c',
    'stm32/common/flash_common_f.c','stm32/common/flash_common_f01.c','cm3/systick.c',
    'usb/usb.c','usb/usb_control.c','usb/usb_standard.c','usb/usb_microsoft.c','usb/usb_bos.c',
    'stm32/st_usbfs_v1.c','stm32/common/st_usbfs_core.c')
$sources += $librarySources | ForEach-Object { Join-Path (Join-Path $vendor 'lib') $_ }
$objects = @()
$index = 0
foreach ($source in $sources) {
    $object = Join-Path $build ("$index-"+[IO.Path]::GetFileNameWithoutExtension($source)+'.o')
    & $compiler @flags -c $source -o $object
    if ($LASTEXITCODE -ne 0) { throw "Erro ao compilar $source" }
    $objects += $object
    $index++
}
$elf = Join-Path $root 'MacroPill.bin.elf'
& $compiler -mcpu=cortex-m3 -mthumb -nostdlib ('-Wl,-T,'+(Join-Path $root 'stm32f103.ld')) '-Wl,--gc-sections' ('-Wl,-Map,'+(Join-Path $build 'MacroPill.map')) @objects -lgcc -o $elf
if ($LASTEXITCODE -ne 0) { throw 'Erro ao montar o firmware' }
& $objcopy -O binary $elf (Join-Path $root 'MacroPill.bin')
if ($LASTEXITCODE -ne 0) { throw 'Erro ao gerar BIN' }
& $objcopy -O ihex $elf (Join-Path $root 'MacroPill.hex')
if ($LASTEXITCODE -ne 0) { throw 'Erro ao gerar HEX' }
Write-Host 'Firmware compilado. Endereço de gravação: 0x08000000.'
