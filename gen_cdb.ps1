$oldValue = $env:XMAKE_IN_PROJECT_GENERATOR
try {
    $env:XMAKE_IN_PROJECT_GENERATOR = "true"
    # 直接执行，不加 $output = ... ，输出会实时显示在终端
    xmake project -k compile_commands 2>&1
} finally {
    $env:XMAKE_IN_PROJECT_GENERATOR = $oldValue
}