#pragma once

#include <fmt/format.h>
#include <fty/split.h>
#include <fty/traits.h>
#include <fty_common_db_dbpath.h>
#include <tntdb.h>

namespace tnt {

// =====================================================================================================================

template <typename T>
struct Arg
{
    std::string_view name;
    T                value;
    bool             isNull = false;
};

template <class, template <class> class>
struct is_instance : public std::false_type
{
};

template <class T, template <class> class U>
struct is_instance<U<T>, U> : public std::true_type
{
};

inline void shutdown()
{
    tntdb::dropCached();
}

// =====================================================================================================================

class Statement;
class ConstIterator;
class Rows;
class Row;

class Connection
{
public:
    Connection();
    Statement prepare(const std::string& sql);

public:
    template <typename... Args>
    Row selectRow(const std::string& queryStr, Args&&... args);

    template <typename... Args>
    Rows select(const std::string& queryStr, Args&&... args);

    template <typename... Args>
    uint execute(const std::string& queryStr, Args&&... args);

    int64_t lastInsertId();

private:
    tntdb::Connection m_connection;
    friend class Transaction;
};

// =====================================================================================================================

class Row
{
public:
    Row() = default;

    template <typename T>
    T get(const std::string& col) const;

    std::string get(const std::string& col) const;

    template <typename T>
    void get(const std::string& name, T& val) const;

private:
    Row(const tntdb::Row& row);

    friend class Statement;
    friend class ConstIterator;
    friend class Rows;
    tntdb::Row m_row;
};

// =====================================================================================================================

class Rows
{
public:
    Rows();

    ConstIterator begin() const;
    ConstIterator end() const;
    size_t        size() const;
    bool          empty() const;
    Row           operator[](size_t off) const;

private:
    Rows(const tntdb::Result& rows);

    tntdb::Result m_rows;
    friend class ConstIterator;
    friend class Statement;
};

// =====================================================================================================================

class ConstIterator : public std::iterator<std::random_access_iterator_tag, Row>
{
public:
    using ConstReference = const value_type&;
    using ConstPointer   = const value_type*;

private:
    Rows   m_rows;
    Row    m_current;
    size_t m_offset;

    void setOffset(size_t off);

public:
    ConstIterator(const Rows& r, size_t off);
    bool            operator==(const ConstIterator& it) const;
    bool            operator!=(const ConstIterator& it) const;
    ConstIterator&  operator++();
    ConstIterator   operator++(int);
    ConstIterator   operator--();
    ConstIterator   operator--(int);
    ConstReference  operator*() const;
    ConstPointer    operator->() const;
    ConstIterator&  operator+=(difference_type n);
    ConstIterator   operator+(difference_type n) const;
    ConstIterator&  operator-=(difference_type n);
    ConstIterator   operator-(difference_type n) const;
    difference_type operator-(const ConstIterator& it) const;
};

// =====================================================================================================================

class Statement
{
public:
    template <typename T>
    Statement& bind(const std::string& name, const T& value);

    template <typename TArg, typename... TArgs>
    Statement& bind(TArg&& val, TArgs&&... args);

    template <typename TArg>
    Statement& bind(Arg<TArg>&& arg);

    template <typename TArg, typename... TArgs>
    Statement& bindMulti(size_t count, TArg&& val, TArgs&&... args);

    template <typename TArg>
    Statement& bindMulti(size_t count, Arg<TArg>&& arg);

    Statement& bind();

public:
    Row  selectRow() const;
    Rows select() const;
    uint execute() const;

private:
    Statement(const tntdb::Statement& st);

    mutable tntdb::Statement m_st;
    friend class Connection;
};

// =====================================================================================================================

class Transaction
{
public:
    Transaction(Connection& con);

    void commit();
    void rollback();

private:
    tntdb::Transaction m_trans;
};

// =====================================================================================================================


inline std::string multiInsert(std::initializer_list<std::string> cols, size_t count)
{
    std::vector<std::string> colsStr;
    for (const auto& col : cols) {
        colsStr.push_back(":" + col + "_{0}");
    }

    std::string out;
    for (size_t i = 0; i < count; ++i) {
        out += (i > 0 ? ", " : "") + fmt::format("(" + fty::implode(colsStr, ", ") + ")", i);
    }
    return out;
}

} // namespace tnt

// =====================================================================================================================
// Arguments
// =====================================================================================================================

namespace tnt::internal {


struct Arg
{
    std::string_view str;

    template <typename T>
    auto operator=(T&& value) const
    {
        if constexpr (is_instance<T, std::optional>::value) {
            if (value) {
                return tnt::Arg<typename T::value_type>{str, std::forward<typename T::value_type>(*value)};
            } else {
                return tnt::Arg<typename T::value_type>{str, typename T::value_type{}, true};
            }
        } else {
            return tnt::Arg<T>{str, std::forward<T>(value)};
        }
    }
};

} // namespace tnt::internal

constexpr tnt::internal::Arg operator"" _p(const char* s, size_t n)
{
    return {{s, n}};
}

template <typename T>
std::optional<std::decay_t<T>> nullable(bool cond, T&& value)
{
    if (cond) {
        return std::optional<std::decay_t<T>>(value);
    }
    return std::nullopt;
}

// =====================================================================================================================
// Connection impl
// =====================================================================================================================

inline tnt::Connection::Connection()
    : m_connection(tntdb::connectCached(getenv("DBURL") ? getenv("DBURL") : DBConn::url))
{
}

inline tnt::Statement tnt::Connection::prepare(const std::string& sql)
{
    return Statement(m_connection.prepareCached(sql));
}

template <typename... Args>
inline tnt::Row tnt::Connection::selectRow(const std::string& queryStr, Args&&... args)
{
    return Statement(m_connection.prepareCached(queryStr)).bind(std::forward<Args>(args)...).selectRow();
}

template <typename... Args>
inline tnt::Rows tnt::Connection::select(const std::string& queryStr, Args&&... args)
{
    return Statement(m_connection.prepareCached(queryStr)).bind(std::forward<Args>(args)...).select();
}

template <typename... Args>
inline uint tnt::Connection::execute(const std::string& queryStr, Args&&... args)
{
    return Statement(m_connection.prepareCached(queryStr)).bind(std::forward<Args>(args)...).execute();
}

inline int64_t tnt::Connection::lastInsertId()
{
    return m_connection.lastInsertId();
}

// =====================================================================================================================
// Statement impl
// =====================================================================================================================

template <typename T>
inline tnt::Statement& tnt::Statement::bind(const std::string& name, const T& value)
{
    m_st.set(name, value);
    return *this;
}

template <typename TArg, typename... TArgs>
inline tnt::Statement& tnt::Statement::bind(TArg&& val, TArgs&&... args)
{
    bind(std::forward<TArg>(val));
    bind(std::forward<TArgs>(args)...);
    return *this;
}

template <typename TArg>
inline tnt::Statement& tnt::Statement::bind(Arg<TArg>&& arg)
{
    if (arg.isNull) {
        m_st.setNull(arg.name.data());
    } else {
        m_st.set(arg.name.data(), arg.value);
    }
    return *this;
}

inline tnt::Statement& tnt::Statement::bind()
{
    return *this;
}


template <typename TArg, typename... TArgs>
inline tnt::Statement& tnt::Statement::bindMulti(size_t count, TArg&& val, TArgs&&... args)
{
    bindMulti(count, std::forward<TArg>(val));
    bindMulti(count, std::forward<TArgs>(args)...);
    return *this;
}

template <typename TArg>
inline tnt::Statement& tnt::Statement::bindMulti(size_t count, Arg<TArg>&& arg)
{
    if (arg.isNull) {
        m_st.setNull(fmt::format("{}_{}", arg.name, count));
    } else {
        m_st.set(fmt::format("{}_{}", arg.name, count), arg.value);
    }
    return *this;
}


inline tnt::Row tnt::Statement::selectRow() const
{
    return Row(m_st.selectRow());
}

inline tnt::Rows tnt::Statement::select() const
{
    return Rows(m_st.select());
}

inline uint tnt::Statement::execute() const
{
    return m_st.execute();
}

inline tnt::Statement::Statement(const tntdb::Statement& st)
    : m_st(st)
{
}

// =====================================================================================================================
// Row impl
// =====================================================================================================================

template <typename T>
inline T tnt::Row::get(const std::string& col) const
{
    if (m_row.isNull(col)) {
        return {};
    }

    try {
        if constexpr (std::is_same_v<T, std::string>) {
            return m_row.getString(col);
        } else if constexpr (std::is_same_v<T, bool>) {
            return m_row.getBool(col);
        } else if constexpr (std::is_same_v<T, int64_t>) {
            return m_row.getInt64(col);
        } else if constexpr (std::is_same_v<T, int32_t>) {
            return m_row.getInt32(col);
        } else if constexpr (std::is_same_v<T, int16_t>) {
            return m_row.getInt(col);
        } else if constexpr (std::is_same_v<T, int8_t>) {
            return int8_t(m_row.getInt(col));
        } else if constexpr (std::is_same_v<T, uint64_t>) {
            return m_row.getUnsigned64(col);
        } else if constexpr (std::is_same_v<T, uint32_t>) {
            return m_row.getUnsigned32(col);
        } else if constexpr (std::is_same_v<T, uint16_t>) {
            return uint16_t(m_row.getUnsigned(col));
        } else if constexpr (std::is_same_v<T, uint8_t>) {
            return uint8_t(m_row.getUnsigned(col));
        } else if constexpr (std::is_same_v<T, float>) {
            return m_row.getFloat(col);
        } else if constexpr (std::is_same_v<T, double>) {
            return m_row.getDouble(col);
        } else {
            static_assert(fty::always_false<T>, "Unsupported type");
        }
    } catch (const tntdb::NullValue&/* e*/) {
        return T{};
    }
}

inline std::string tnt::Row::get(const std::string& col) const
{
    if (m_row.isNull(col)) {
        return {};
    } else {
        return m_row.getString(col);
    }
}

template <typename T>
inline void tnt::Row::get(const std::string& name, T& val) const
{
    val = get<std::decay_t<T>>(name);
}

inline tnt::Row::Row(const tntdb::Row& row)
    : m_row(row)
{
}

// =====================================================================================================================
// Rows iterator impl
// =====================================================================================================================

inline void tnt::ConstIterator::setOffset(size_t off)
{
    if (off != m_offset) {
        m_offset = off;
        if (m_offset < m_rows.m_rows.size())
            m_current = m_rows.m_rows.getRow(unsigned(m_offset));
    }
}

inline tnt::ConstIterator::ConstIterator(const Rows& r, size_t off)
    : m_rows(r)
    , m_offset(off)
{
    if (m_offset < r.m_rows.size())
        m_current = r.m_rows.getRow(unsigned(m_offset));
}

inline bool tnt::ConstIterator::operator==(const ConstIterator& it) const
{
    return m_offset == it.m_offset;
}

inline bool tnt::ConstIterator::operator!=(const ConstIterator& it) const
{
    return !operator==(it);
}

inline tnt::ConstIterator& tnt::ConstIterator::operator++()
{
    setOffset(m_offset + 1);
    return *this;
}

inline tnt::ConstIterator tnt::ConstIterator::operator++(int)
{
    ConstIterator ret = *this;
    setOffset(m_offset + 1);
    return ret;
}

inline tnt::ConstIterator tnt::ConstIterator::operator--()
{
    setOffset(m_offset - 1);
    return *this;
}

inline tnt::ConstIterator tnt::ConstIterator::operator--(int)
{
    ConstIterator ret = *this;
    setOffset(m_offset - 1);
    return ret;
}

inline tnt::ConstIterator::ConstReference tnt::ConstIterator::operator*() const
{
    return m_current;
}

inline tnt::ConstIterator::ConstPointer tnt::ConstIterator::operator->() const
{
    return &m_current;
}

inline tnt::ConstIterator& tnt::ConstIterator::operator+=(difference_type n)
{
    setOffset(m_offset + size_t(n));
    return *this;
}

inline tnt::ConstIterator tnt::ConstIterator::operator+(difference_type n) const
{
    ConstIterator it(*this);
    it += n;
    return it;
}

inline tnt::ConstIterator& tnt::ConstIterator::operator-=(difference_type n)
{
    setOffset(m_offset - size_t(n));
    return *this;
}

inline tnt::ConstIterator tnt::ConstIterator::operator-(difference_type n) const
{
    ConstIterator it(*this);
    it -= n;
    return it;
}

inline tnt::ConstIterator::difference_type tnt::ConstIterator::operator-(const ConstIterator& it) const
{
    return tnt::ConstIterator::difference_type(m_offset - it.m_offset);
}

// =====================================================================================================================
// Rows impl
// =====================================================================================================================

inline tnt::ConstIterator tnt::Rows::begin() const
{
    return tnt::ConstIterator(*this, 0);
}

inline tnt::ConstIterator tnt::Rows::end() const
{
    return tnt::ConstIterator(*this, size());
}

inline size_t tnt::Rows::size() const
{
    return m_rows.size();
}

inline bool tnt::Rows::empty() const
{
    return m_rows.empty();
}

inline tnt::Row tnt::Rows::operator[](size_t off) const
{
    return tnt::Row(m_rows.getRow(unsigned(off)));
}

inline tnt::Rows::Rows(const tntdb::Result& rows)
    : m_rows(rows)
{
}

inline tnt::Rows::Rows()
{
}


// =====================================================================================================================
// Transaction impl
// =====================================================================================================================

inline tnt::Transaction::Transaction(Connection& con)
    : m_trans(tntdb::Transaction(con.m_connection))
{
}

inline void tnt::Transaction::commit()
{
    m_trans.commit();
}

inline void tnt::Transaction::rollback()
{
    m_trans.rollback();
}
