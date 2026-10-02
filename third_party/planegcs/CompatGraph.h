// CadForge replacement for the tiny part of Boost.Graph used by PlaneGCS
// (undirected graph + connected components), so no Boost dependency is needed.
#pragma once
#include <numeric>
#include <vector>

namespace boost {
struct vecS {};
struct undirectedS {};

template <class, class, class>
class adjacency_list {
public:
    std::vector<int> parent;
    int find(int v)
    {
        while (parent[std::size_t(v)] != v) {
            parent[std::size_t(v)] = parent[std::size_t(parent[std::size_t(v)])];
            v = parent[std::size_t(v)];
        }
        return v;
    }
};

template <class A, class B, class C>
inline int add_vertex(adjacency_list<A, B, C>& g)
{
    g.parent.push_back(int(g.parent.size()));
    return int(g.parent.size()) - 1;
}

template <class A, class B, class C>
inline void add_edge(int u, int v, adjacency_list<A, B, C>& g)
{
    const int ru = g.find(u), rv = g.find(v);
    if (ru != rv)
        g.parent[std::size_t(ru)] = rv;
}

template <class A, class B, class C>
inline std::size_t num_vertices(const adjacency_list<A, B, C>& g)
{
    return g.parent.size();
}

/// Labels components 0..k-1 in order of their first vertex; returns k.
template <class A, class B, class C>
inline int connected_components(adjacency_list<A, B, C>& g, int* component)
{
    std::vector<int> label(g.parent.size(), -1);
    int count = 0;
    for (std::size_t v = 0; v < g.parent.size(); ++v) {
        const int r = g.find(int(v));
        if (label[std::size_t(r)] < 0)
            label[std::size_t(r)] = count++;
        component[v] = label[std::size_t(r)];
    }
    return count;
}
} // namespace boost
