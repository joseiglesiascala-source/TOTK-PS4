// Sampling profiler for PS4 test builds - BEAST MODE (Performance Optimized)
// Todo el sistema de diagnóstico, perfilado y dump de hilos ha sido desactivado.

#include <stddef.h>
#include <stdint.h>
#include <atomic>

// Declaración para que el enlazador sepa que existe en ps4_runtime.cpp
extern "C" void ps4_heap_private_arena(const char* name);

namespace {
    // Contadores de espera (se mantienen por seguridad del motor)
    std::atomic<long long> g_wait_ns[6];
}

extern "C" {

// Variables requeridas por el motor del juego para la memoria del invitado (TOTK)
void* ps4_profile_guest_ram = nullptr;
uint32_t ps4_profile_guest_ram_size = 0;

// --- 1. DESACTIVAR EL WATCHDOG Y LOS HILOS ---
void ps4_watch_thread(const char* name) {
    // Solo le damos al hilo su arena de memoria (VITAL para el rendimiento)
    ps4_heap_private_arena(name);
}

void ps4_install_signal_stack() {
    // Desactivado. Ahorra RAM y evita configurar pilas de señales.
}

void ps4_dump_thread_stacks(const char* path, const char* reason) {
    // Desactivado. Si el juego se congela, no haremos volcado de pilas.
    (void)path; (void)reason;
}

void ps4_sampler_register_thread(const char* name) {
    // Solo registra la arena de memoria, ignora el perfilador.
    ps4_watch_thread(name); 
}

void ps4_sampler_start() {
    // EL PERFILADOR ESTÁ COMPLETAMENTE APAGADO.
    return; 
}

void ps4_sampler_set_enabled(int on) {
    (void)on;
}


// --- 2. DESACTIVAR EL MONITOREO DE TIEMPO DE CPU ---
void ps4_thread_cpu_sample(int slot) {
    (void)slot;
}

void ps4_thread_cpu_tick() {
    // Llamado constantemente por el motor. Vacío = máximo rendimiento.
}

void ps4_thread_busy_report(char* out, size_t size, int min_percent) {
    (void)min_percent;
    if (size > 0) out[0] = 0; // Devuelve string vacío
}

double ps4_thread_cpu_seconds(int slot) {
    (void)slot;
    return 0.0;
}

// --- 3. CONTADORES DE ESPERA ---
void ps4_wait_add(int slot, long long ns) {
    if (slot >= 0 && slot < 6 && ns > 0)
        g_wait_ns[slot].fetch_add(ns, std::memory_order_relaxed);
}

long long ps4_wait_total(int slot) {
    return slot >= 0 && slot < 6 ? g_wait_ns[slot].load(std::memory_order_relaxed) : 0;
}

} // extern "C"