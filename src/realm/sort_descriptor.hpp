/*************************************************************************
 *
 * Copyright 2016 Realm Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 **************************************************************************/

#ifndef REALM_SORT_DESCRIPTOR_HPP
#define REALM_SORT_DESCRIPTOR_HPP

#include <vector>
#include <realm/cluster.hpp>
#include <realm/handover_defs.hpp>
#include <realm/mixed.hpp>

namespace realm {

class SortDescriptor;
class ConstTableRef;

// CommonDescriptor encapsulates a reference to a set of columns (possibly over
// links), which is used to indicate the criteria columns for sort and distinct.
// Although the input is column indices, it does not rely on those indices
// remaining stable as long as the columns continue to exist.
class CommonDescriptor {
public:
    struct IndexPair {
        IndexPair(ObjKey k, size_t i)
            : key_for_object(k)
            , index_in_view(i)
        {
        }
        ObjKey key_for_object;
        size_t index_in_view;
        Mixed cached_value;
    };
    struct ColumnId {
        const Table* table;
        ColKey col_key;
    };
    using IndexPairs = std::vector<CommonDescriptor::IndexPair>;

    CommonDescriptor() = default;
    // Enforce type saftey to prevent automatic conversion of derived class
    // SortDescriptor into CommonDescriptor at compile time.
    CommonDescriptor(const SortDescriptor&) = delete;
    virtual ~CommonDescriptor() = default;

    // Create a descriptor for the given columns on the given table.
    // Each vector in `column_indices` represents a chain of columns, where
    // all but the last are Link columns (n.b.: LinkList and Backlink are not
    // supported), and the final is any column type that can be sorted on.
    // `column_indices` must be non-empty, and each vector within it must also
    // be non-empty.
    CommonDescriptor(Table const& table, std::vector<std::vector<ColKey>> column_indices);
    virtual std::unique_ptr<CommonDescriptor> clone() const;

    // returns whether this descriptor is valid and can be used to sort
    bool is_valid() const noexcept
    {
        return !m_column_ids.empty();
    }

    class Sorter {
    public:
        Sorter(std::vector<std::vector<ColumnId>> const& columns, std::vector<bool> const& ascending,
               KeyColumn const& row_indexes);
        Sorter()
        {
        }

        bool operator()(IndexPair i, IndexPair j, bool total_ordering = true) const;

        bool has_links() const
        {
            return std::any_of(m_columns.begin(), m_columns.end(),
                               [](auto&& col) { return !col.translated_keys.empty(); });
        }

        bool any_is_null(IndexPair i) const
        {
            return std::any_of(m_columns.begin(), m_columns.end(), [=](auto&& col) {
                return col.is_null.empty() ? false : col.is_null[i.index_in_view];
            });
        }
        void cache_first_column(IndexPairs& v);

    private:
        struct SortColumn {
            SortColumn(const Table* t, ColKey c, bool a)
                : table(t)
                , col_key(c)
                , ascending(a)
            {
            }
            std::vector<bool> is_null;
            std::vector<ObjKey> translated_keys;

            const Table* table;
            ColKey col_key;
            bool ascending;
        };
        std::vector<SortColumn> m_columns;
        friend class ObjList;
    };
    virtual bool is_sort() const
    {
        return false;
    }
    virtual Sorter sorter(KeyColumn const& row_indexes) const;
    // Do what you have to do
    virtual void execute(IndexPairs& v, const Sorter& predicate, const CommonDescriptor* next) const;

    // handover support
    std::vector<std::vector<ColKey>> export_column_indices() const;
    virtual std::vector<bool> export_order() const
    {
        return {};
    }
    virtual std::string get_description(ConstTableRef attached_table) const;

protected:
    std::vector<std::vector<ColumnId>> m_column_ids;
};

class SortDescriptor : public CommonDescriptor {
public:
    // Create a sort descriptor for the given columns on the given table.
    // See CommonDescriptor for restrictions on `column_indices`.
    // The sort order can be specified by using `ascending` which must either be
    // empty or have one entry for each column index chain.
    SortDescriptor(Table const& table, std::vector<std::vector<ColKey>> column_indices,
                   std::vector<bool> ascending = {});
    SortDescriptor() = default;
    ~SortDescriptor() = default;
    std::unique_ptr<CommonDescriptor> clone() const override;

    void merge_with(SortDescriptor&& other);

    bool is_sort() const override
    {
        return true;
    }
    Sorter sorter(KeyColumn const& row_indexes) const override;

    void execute(IndexPairs& v, const Sorter& predicate, const CommonDescriptor* next) const override;

    // handover support
    std::vector<bool> export_order() const override;
    std::string get_description(ConstTableRef attached_table) const override;

private:
    std::vector<bool> m_ascending;
};

// Distinct uses the same syntax as sort except that the order is meaningless.
typedef CommonDescriptor DistinctDescriptor;

class DescriptorOrdering {
public:
    DescriptorOrdering() = default;
    DescriptorOrdering(const DescriptorOrdering&);
    DescriptorOrdering(DescriptorOrdering&&) = default;
    DescriptorOrdering& operator=(const DescriptorOrdering&);
    DescriptorOrdering& operator=(DescriptorOrdering&&) = default;

    void append_sort(SortDescriptor sort);
    void append_distinct(DistinctDescriptor distinct);
    bool is_empty() const
    {
        return m_descriptors.empty();
    }
    size_t size() const
    {
        return m_descriptors.size();
    }
    const CommonDescriptor* operator[](size_t ndx) const;
    bool will_apply_sort() const;
    bool will_apply_distinct() const;
    std::string get_description(ConstTableRef target_table) const;

    // handover support
    using HandoverPatch = std::unique_ptr<DescriptorOrderingHandoverPatch>;
    static void generate_patch(DescriptorOrdering const&, HandoverPatch&);
    static DescriptorOrdering create_from_and_consume_patch(HandoverPatch&, Table const&);

private:
    std::vector<std::unique_ptr<CommonDescriptor>> m_descriptors;
};
}

#endif /* REALM_SORT_DESCRIPTOR_HPP */
