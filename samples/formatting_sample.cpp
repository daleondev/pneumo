#include "pneumo/formatting.hpp"

#include <print>

// NOLINTBEGIN

// ==========================================
// 1. Aggregates (Automatic Reflection)
// ==========================================
struct Point
{
    int x;
    int y;
};

struct Config
{
    int id;
    std::string name;
    std::vector<double> values;
    Point resolution;
    bool is_active;
};

// ==========================================
// 2. Encapsulated Classes (via pnm::fmt::Adapter)
// ==========================================
class User
{
  public:
    User(std::string name, std::string role, int level)
      : m_name(std::move(name))
      , m_role(std::move(role))
      , m_level(level)
    {
    }

    const std::string& getName() const { return m_name; }
    const std::string& getRole() const { return m_role; }
    int getLevel() const { return m_level; }

  private:
    std::string m_name;
    std::string m_role;
    int m_level;
};

template<>
struct pnm::fmt::Adapter<User>
{
    using Fields = std::tuple<pnm::fmt::Field<"name", &User::getName>,
                              pnm::fmt::Field<"role", &User::getRole>,
                              pnm::fmt::Field<"level", &User::getLevel>>;
};

// ==========================================
// 3. Scoped Enums
// ==========================================
enum class Status
{
    Idle,
    Processing,
    Completed,
    Failed
};

// ==========================================
// 4. Manually formatted classes (ostream/toString())
// ==========================================

class Storage
{
  public:
    Storage(const char* data, size_t size)
      : m_size(size)
    {
        std::copy_n(data, size, m_data.data());
    }

    size_t size() const { return m_size; }
    const char* rawData() const { return m_data.data(); }

  private:
    size_t m_size;
    std::array<char, 20> m_data{};
};

std::ostream& operator<<(std::ostream& os, const Storage& s)
{
    return os << "The storage contains: '" << std::string(s.rawData(), s.size()) << "'";
}

class Platform
{
  public:
    const char* toString() const
    {
#ifdef _WIN32
        return "The current platform is Windows";
#else
        return "The current platform is Linux";
#endif
    }
};

int main()
{
    std::println("=========================================");
    std::println("   Format Utils Library Showcase");
    std::println("=========================================\n");

    // -------------------------------------------------
    // Scenario 1: Automatic Reflection for Aggregates
    // -------------------------------------------------
    std::println("--- 1. Automatic Reflection (Structs) ---");
    Config cfg{ 101, "SimulationConfig", { 0.5, 1.2, 3.14 }, { 1920, 1080 }, true };

    std::println("Default: {}", cfg);
    std::println("Pretty: \n{:p}", cfg);
    std::println("");

    // -------------------------------------------------
    // Scenario 2: Adapters for Private Members
    // -------------------------------------------------
    std::println("--- 2. Adapters (Classes with private data) ---");
    User user("Alice", "Administrator", 99);

    std::println("User (Default): {}", user);
    std::println("User (Pretty): \n{:p}", user);
    std::println("");

    // -------------------------------------------------
    // Scenario 3: Serialization (JSON / YAML / TOML)
    // -------------------------------------------------
    std::println("--- 3. Serialization (Glaze Integration) ---");

#ifdef PFMT_ENABLE_JSON
    std::println("Compact JSON: {:j}", user);
    std::println("Pretty JSON: \n{:pj}", user);
    std::println("");
#endif

#ifdef PFMT_ENABLE_YAML
    std::println("YAML: \n{:y}", user);
    std::println("");
#endif

#ifdef PFMT_ENABLE_TOML
    std::println("TOML: \n{:t}", user);
    std::println("");
#endif

    // -------------------------------------------------
    // Scenario 4: Enums
    // -------------------------------------------------
    std::println("--- 4. Scoped Enums ---");
    Status current_status = Status::Processing;

    std::println("Status (Default): {}", current_status);
    std::println("Status (Verbose): {:v}", current_status);
    std::println("");

    // -------------------------------------------------
    // Scenario 5: Pointers and Optionals
    // -------------------------------------------------
    std::println("--- 5. Pointers, Smart Pointers & Optionals ---");

    // Optional
    std::optional<Point> opt_point;
    std::println("Empty Optional:      {}", opt_point);
    opt_point = Point{ 10, 20 };
    std::println("Filled Optional:     {}", opt_point);

    // Raw Nullptr
    User* raw_ptr = nullptr;
    std::println("Null Ptr:            {}", raw_ptr);

    // Raw Pointer
    raw_ptr = new User("Bob", "Guest", 1);
    std::println("Raw Ptr:             {}", raw_ptr);

    // Smart Nullptr
    std::unique_ptr<User> smart_ptr = nullptr;
    std::println("Smart Null Ptr:      {}", smart_ptr);

    // Smart Pointer
    smart_ptr.reset(raw_ptr);
    std::println("Smart Ptr:           {}", smart_ptr);

    std::println("\n=========================================");

    // -------------------------------------------------
    // Scenario 6: Manual formatting (ostream/toString())
    // -------------------------------------------------
    std::println("--- 6. Manual formatting (ostream/toString()) ---");

    Storage store{ "Hello World!\0", 13 };
    std::println("ostream:             {}", store);

    std::println("toString():          {}", Platform{});

    std::println("\n=========================================");

    return 0;
}

// NOLINTEND
