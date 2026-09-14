# Pop-анимация "!": масштаб 0.05 -> 1.5 за 5 тиков (отдельный тик нужен, чтобы сработала интерполяция).
execute as @e[type=minecraft:text_display,tag=kj_mark] run data modify entity @s transformation.scale set value [1.5f,1.5f,1.5f]
