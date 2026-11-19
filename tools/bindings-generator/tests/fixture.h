// Generator contract fixture: intentionally independent of any SDK headers.
namespace std {
template<class Signature> class function {};
template<class T, unsigned long Extent = ~0UL> class span {};
class string_view {};
}
namespace ax {
namespace rhi { enum class GeneratorFormat { VALUE = 4 }; }
class GeneratorBase {
public:
    GeneratorBase() = default;
    virtual ~GeneratorBase() = default;
    virtual int value() const { return 1; }
    void setNumber(int) {}
};
namespace ext { using GeneratorBaseAlias = GeneratorBase; }
namespace extension = ext;
class GeneratorNode : public GeneratorBase {
public:
    enum class Mode { A = 1, B = 3 };
    GeneratorNode() = default;
    GeneratorNode(int, bool = true) {}
    GeneratorNode(const char*, const char* = 0) {}
    int value() const override { return 2; }
    void move(float, float = 0) {}
    void move(GeneratorBase*) {}
    void format(rhi::GeneratorFormat value = rhi::GeneratorFormat::VALUE) {}
    using FormatAlias = rhi::GeneratorFormat;
    void aliasFormat(FormatAlias) {}
    void selection(bool) {}
    void selection(FormatAlias, bool = true) {}
    /** @script{ignore} */
    void manualOnly(int) {}
    void setName(const char*) {}
    ax::extension::GeneratorBaseAlias* extensionBase() { return nullptr; }
    static GeneratorNode* create(int = 4) { return nullptr; }
    GeneratorNode* clone() const { return nullptr; }
    GeneratorNode* reverse(int = 0) const { return nullptr; }
    Mode mode() const { return Mode::A; }
    using Callback = std::function<void(int)>;
    void setCallback(const Callback&) {}
    void observe(const Callback&, int = 1) {}
    void consumeSpan(std::span<const int>, int = 1) {}
    void inspectSpan(const std::span<const int>&) {}
    std::span<const int> getSpan() const { return {}; }
    void mutateSpan(std::span<int>&) {}
    void volatileSpan(volatile std::span<int>&) {}
    void consumeStringViews(std::span<const std::string_view>) {}
    int count;
    Callback callback;
};
}
