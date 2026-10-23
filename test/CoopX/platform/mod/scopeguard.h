#ifndef __SCOPEGUARD_H
#define __SCOPEGUARD_H





















template<typename F>
class ScopeGuard
{
public:
    explicit ScopeGuard(F fn) : m_fn(std::move(fn)), m_active(true) {}
    ~ScopeGuard() { if(m_active) m_fn(); }
    ScopeGuard(ScopeGuard&& o) noexcept : m_fn(std::move(o.m_fn)), m_active(o.m_active) { o.m_active = false; }
    ScopeGuard(const ScopeGuard&) = delete;
    ScopeGuard& operator=(const ScopeGuard&) = delete;
    void Dismiss() { m_active = false; }

private:
    F    m_fn;
    bool m_active;
};
template<typename F>
ScopeGuard<F> MakeScopeGuard(F f)
{
    return ScopeGuard<F>(std::move(f));
}

#define __DEFER_CAT(a,b) a##b
#define __DEFER_CAT2(a,b) __DEFER_CAT(a,b)
#define DEFER(code) auto __DEFER_CAT2(__defer_, __COUNTER__) = MakeScopeGuard([&](){ code; })

#endif