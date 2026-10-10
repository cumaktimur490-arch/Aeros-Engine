# Aeros Engine Lite Android — ProGuard rules для минимального APK
# Сохраняем native методы
-keep class com.aos.aerosengine.** { *; }
-keepclasseswithmembernames class * {
    native <methods>;
}
# Убираем логи в релизе для производительности
-assumenosideeffects class android.util.Log {
    public static *** d(...);
    public static *** v(...);
    public static *** i(...);
}
# Оптимизации для размера
-optimizationpasses 5
-allowaccessmodification
-mergeinterfacesaggressively
# Для старых телефонов — не обфусцировать слишком сильно
-dontobfuscate
# Сохраняем NativeActivity
-keep class android.app.NativeActivity { *; }
