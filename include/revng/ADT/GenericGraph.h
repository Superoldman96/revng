#pragma once

//
// This file is distributed under the MIT License. See LICENSE.md for details.
//

#include <type_traits>

#include "llvm/ADT/GraphTraits.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"

#include "revng/ADT/STLExtras.h"
#include "revng/Support/Debug.h"

/// GenericGraph is our implementation of a universal graph architecture.
/// First and foremost, it's tailored for the revng codebase, but maybe you
/// can find it useful as well, that's why it's released under the MIT License.
///
/// To use the Graph, you need to make you choice of the node type. We're
/// currently supporting three architectures:
///
///   - ForwardNode - a trivially-simple single-linked node. It uses the least
///     memory, because each node only stores the list of its successors.
///     This makes backwards iteration impossible without the reference to
///     the whole graph (and really expensive even in those cases).
///
///   - BidirectionalNode - a simple double-linked node. It stores the both
///     lists of its successors and predecessors. Take note, that for the sake
///     of implementation simplicity this node stores COPIES of the labels.
///     It's only suitable to be used with cheap to copy labels which are never
///     mutated. There are plans on making them explicitly immutable (TODO),
///     as such you can consider label mutation a deprecated behaviour.
///
///   - MutableEdgeNode - a double-linked node with dynamically allocated
///     labels. It is similar to BidirectionalNode except it stores the label
///     on the heap. It's slower and uses more memory but allows for safe label
///     modification as well as controls that nodes and edges are removed
///     safely, with the other "halfs" cleaned up as well.
///     Note that it explicitly disallows having more than one edge
///     per direction between a pair of nodes.
///
///   - The node you're going to write - No-one knows the needs of your
///     project better than you. That's why the best datastructure is the one
///     you are going to write. So just inherit one of our nodes, or even copy
///     it and modify it so that it suits your graphs as nicely as possible.
///
///  On the side-note, we're providing a couple of helpful concepts to help
///  differenciate different node types. This is helpful in the projects that
///  use multiple different graph architectures side by side.

template<typename T>
concept IsForwardNode = requires {
  T::is_forward_node;
};

template<typename T>
concept IsBidirectionalNode = requires {
  T::is_bidirectional_node;
  typename llvm::Inverse<T *>;
};

template<typename T>
concept IsMutableEdgeNode = requires {
  T::is_mutable_edge_node;
  typename llvm::Inverse<T *>;
};

template<typename T>
concept IsGenericGraph = requires {
  T::is_generic_graph;
  typename T::Node;
};

struct Empty {
  bool operator==(const Empty &) const { return true; }
};

template<typename Node, size_t SmallSize = 16, bool HasEntryNode = true>
class GenericGraph;

template<typename T, typename BaseType>
class Parent : public BaseType {
public:
  template<typename... Args>
  explicit Parent(Args &&...args) :
    BaseType(std::forward<Args>(args)...), TheParent(nullptr) {}

  Parent(const Parent &) = default;
  Parent(Parent &&) = default;

private:
  T *TheParent;

public:
  T *getParent() const { return TheParent; }
  void setParent(T *Parent) { this->TheParent = Parent; }
};

/// Data structure for edge labels
template<typename Node, typename EdgeLabel>
struct Edge : public EdgeLabel {
  Edge(Node *Neighbor) : Neighbor(Neighbor) {}
  Edge(Node *Neighbor, EdgeLabel EL) : EdgeLabel(EL), Neighbor(Neighbor) {}
  Node *Neighbor;
};

//
// Structured binding support for Edge
//
template<size_t I, typename Node, typename EdgeLabel>
auto &get(Edge<Node, EdgeLabel> &E) {
  if constexpr (I == 0)
    return E.Neighbor;
  else
    return static_cast<EdgeLabel &>(E);
}

template<size_t I, typename Node, typename EdgeLabel>
auto &get(const Edge<Node, EdgeLabel> &E) {
  if constexpr (I == 0)
    return E.Neighbor;
  else
    return static_cast<EdgeLabel &>(E);
}

template<size_t I, typename Node, typename EdgeLabel>
auto &&get(Edge<Node, EdgeLabel> &&E) {
  if constexpr (I == 0)
    return std::move(E.Neighbor);
  else
    return std::move(static_cast<EdgeLabel &&>(E));
}

namespace std {
template<typename Node, typename EdgeLabel>
struct std::tuple_size<Edge<Node, EdgeLabel>>
  : std::integral_constant<size_t, 2> {};

template<typename Node, typename EdgeLabel>
struct std::tuple_element<0, Edge<Node, EdgeLabel>> {
  using type = Node *;
};

template<typename Node, typename EdgeLabel>
struct std::tuple_element<1, Edge<Node, EdgeLabel>> {
  using type = EdgeLabel;
};

} // namespace std

namespace detail {
/// We require to operate some decision to select a base type that Forward node
/// will extend. Those decisions are wrapped inside this struct to remove
/// clutter.
///
/// \note At this step Forward edge has not been declared yet, thus we accept a
///       template parameter that has the same signature as ForwardEdge that
///       will be declared later. This allows us to use it as if it was
///       delcared, provided that only the real ForwardEdge is used as this
///       argument.
template<typename Node,
         typename EdgeLabel,
         bool HasParent,
         size_t SmallSize,
         template<typename, typename, bool, size_t, typename, size_t, bool>
         class ForwardEdge,
         typename FinalType,
         size_t ParentSmallSize,
         bool ParentHasEntryNode>
struct ForwardNodeBaseTCalc {
  static constexpr bool
    NoDerivation = std::is_same_v<FinalType, std::false_type>;
  using FWNode = ForwardEdge<Node,
                             EdgeLabel,
                             HasParent,
                             SmallSize,
                             FinalType,
                             ParentSmallSize,
                             ParentHasEntryNode>;
  using DerivedType = std::conditional_t<NoDerivation, FWNode, FinalType>;
  using GenericGraph = GenericGraph<DerivedType,
                                    ParentSmallSize,
                                    ParentHasEntryNode>;
  using ParentType = Parent<GenericGraph, Node>;
  using Result = std::conditional_t<HasParent, ParentType, Node>;
};
} // namespace detail

/// Basic nodes type, only forward edges, possibly with parent
template<typename Node,
         typename EdgeLabel = Empty,
         bool HasParent = true,
         size_t SmallSize = 2,
         typename FinalType = std::false_type,
         size_t ParentSmallSize = 16,
         bool ParentHasEntryNode = true>
class ForwardNode
  : public detail::ForwardNodeBaseTCalc<Node,
                                        EdgeLabel,
                                        HasParent,
                                        SmallSize,
                                        ForwardNode,
                                        FinalType,
                                        ParentSmallSize,
                                        ParentHasEntryNode>::Result {
public:
  static constexpr bool is_forward_node = true;
  static constexpr bool has_parent = HasParent;
  using TypeCalc = detail::ForwardNodeBaseTCalc<Node,
                                                EdgeLabel,
                                                HasParent,
                                                SmallSize,
                                                ForwardNode,
                                                FinalType,
                                                ParentSmallSize,
                                                ParentHasEntryNode>;
  using DerivedType = typename TypeCalc::DerivedType;
  using Base = typename TypeCalc::Result;
  using Edge = Edge<DerivedType, EdgeLabel>;

public:
  template<typename... Args>
  explicit ForwardNode(Args &&...args) : Base(std::forward<Args>(args)...) {}

  ForwardNode(const ForwardNode &) = default;
  ForwardNode(ForwardNode &&) = default;

public:
  static DerivedType *&getNeighbor(Edge &E) { return E.Neighbor; }
  static const DerivedType *const &getConstNeighbor(const Edge &E) {
    return E.Neighbor;
  }

  static Edge *getEdgePointer(Edge &E) { return &E; }
  static const Edge *getConstEdgePointer(const Edge &E) { return &E; }

private:
  using iterator_filter = decltype(&getNeighbor);
  using const_iterator_filter = decltype(&getConstNeighbor);

public:
  using NeighborContainer = llvm::SmallVector<Edge, SmallSize>;
  using child_iterator = llvm::mapped_iterator<Edge *, iterator_filter>;
  using const_child_iterator = llvm::mapped_iterator<const Edge *,
                                                     const_iterator_filter>;
  using edge_iterator = typename NeighborContainer::iterator;
  using const_edge_iterator = typename NeighborContainer::const_iterator;

public:
  // This stuff is needed by the DominatorTree implementation
  void printAsOperand(llvm::raw_ostream &, bool) const { revng_abort(); }

public:
  void addSuccessor(DerivedType *NewSuccessor) {
    Successors.emplace_back(NewSuccessor);
  }

  void addSuccessor(DerivedType *NewSuccessor, EdgeLabel EL) {
    Successors.emplace_back(NewSuccessor, EL);
  }

public:
  child_iterator removeSuccessor(child_iterator It) {
    auto InternalIt = Successors.erase(It.getCurrent());
    return child_iterator(InternalIt, getNeighbor);
  }

  edge_iterator removeSuccessorEdge(edge_iterator It) {
    return Successors.erase(It);
  }

public:
  llvm::iterator_range<const_child_iterator> successors() const {
    return toNeighborRange(Successors);
  }

  llvm::iterator_range<child_iterator> successors() {
    return toNeighborRange(Successors);
  }

  llvm::iterator_range<const_edge_iterator> successor_edges() const {
    return llvm::make_range(Successors.begin(), Successors.end());
  }

  llvm::iterator_range<edge_iterator> successor_edges() {
    return llvm::make_range(Successors.begin(), Successors.end());
  }

  bool hasSuccessors() const { return Successors.size() != 0; }
  size_t successorCount() const { return Successors.size(); }

protected:
  static llvm::iterator_range<child_iterator>
  toNeighborRange(NeighborContainer &Neighbors) {
    auto Range = llvm::make_range(Neighbors.begin(), Neighbors.end());
    return llvm::map_range(Range, &getNeighbor);
  }

  static llvm::iterator_range<const_child_iterator>
  toNeighborRange(const NeighborContainer &Neighbors) {
    auto Range = llvm::make_range(Neighbors.begin(), Neighbors.end());
    return llvm::map_range(Range, &getConstNeighbor);
  }

private:
  NeighborContainer Successors;
};

namespace detail {

/// To remove clutter from BidirectionalNode, the computation of some types are
/// done in this class.
///
/// \note At this step BidirectionalEdge has not been declared yet, thus we
///       accept a template parameter that has the same signature as
///       BidirecationEdge that will be declared later. This allows us to use it
///       as if it was delcared, provided that only the real BidirecationalEdge
///       is used as this argument.
template<typename Node,
         typename EdgeLabel,
         bool HasParent,
         size_t SmallSize,
         template<typename, typename, bool, size_t>
         class BidirectionalNode>
struct BidirectionalNodeBaseTCalc {
  using BDNode = BidirectionalNode<Node, EdgeLabel, HasParent, SmallSize>;
  using Result = ForwardNode<Node, EdgeLabel, HasParent, SmallSize, BDNode>;
};
} // namespace detail

/// Same as ForwardNode, but with backward links too
/// TODO: Make edge labels immutable
template<typename Node,
         typename EdgeLabel = Empty,
         bool HasParent = true,
         size_t SmallSize = 2>
class BidirectionalNode
  : public detail::BidirectionalNodeBaseTCalc<Node,
                                              EdgeLabel,
                                              HasParent,
                                              SmallSize,
                                              BidirectionalNode>::Result {
public:
  static const bool is_bidirectional_node = true;

public:
  using NodeData = Node;
  using EdgeLabelData = EdgeLabel;
  using Base = ForwardNode<Node,
                           EdgeLabel,
                           HasParent,
                           SmallSize,
                           BidirectionalNode>;
  using NeighborContainer = typename Base::NeighborContainer;
  using child_iterator = typename Base::child_iterator;
  using const_child_iterator = typename Base::const_child_iterator;
  using edge_iterator = typename Base::edge_iterator;
  using const_edge_iterator = typename Base::const_edge_iterator;

public:
  template<typename... Args>
  explicit BidirectionalNode(Args &&...args) :
    Base(std::forward<Args>(args)...) {}

  BidirectionalNode(const BidirectionalNode &) = default;
  BidirectionalNode(BidirectionalNode &&) = default;

public:
  void addSuccessor(BidirectionalNode *NewSuccessor) {
    Base::addSuccessor({ NewSuccessor });
    NewSuccessor->Predecessors.emplace_back(this);
  }

  void addSuccessor(BidirectionalNode *NewSuccessor, EdgeLabel EL) {
    Base::addSuccessor(NewSuccessor, EL);
    NewSuccessor->Predecessors.emplace_back(this, EL);
  }

  void addPredecessor(BidirectionalNode *NewPredecessor) {
    Predecessors.emplace_back(NewPredecessor);
    NewPredecessor->addSuccessor(this);
  }

  void addPredecessor(BidirectionalNode *NewPredecessor, EdgeLabel EL) {
    Predecessors.emplace_back(NewPredecessor, EL);
    NewPredecessor->addSuccessor(this, EL);
  }

public:
  child_iterator removePredecessor(child_iterator It) {
    auto InternalIt = Predecessors.erase(It.getCurrent());
    return child_iterator(InternalIt, Base::getNeighbor);
  }

  edge_iterator removePredecessorEdge(edge_iterator It) {
    return Predecessors.erase(It);
  }

public:
  llvm::iterator_range<const_child_iterator> predecessors() const {
    return this->toNeighborRange(Predecessors);
  }

  llvm::iterator_range<child_iterator> predecessors() {
    return this->toNeighborRange(Predecessors);
  }

  llvm::iterator_range<const_edge_iterator> predecessor_edges() const {
    return llvm::make_range(Predecessors.begin(), Predecessors.end());
  }

  llvm::iterator_range<edge_iterator> predecessor_edges() {
    return llvm::make_range(Predecessors.begin(), Predecessors.end());
  }

  bool hasPredecessors() const { return Predecessors.size() != 0; }
  size_t predecessorCount() const { return Predecessors.size(); }

private:
  NeighborContainer Predecessors;
};

namespace detail {
/// The parameters deciding specifics of the base type `MutableEdgeNode`
/// extends are non-trivial. That's why those decisiion were wrapped
/// inside this struct to minimize clutter.
///
/// \note At this step `TheNode` has not been declared yet, thus we accept a
///       template parameter that has the same signature as `TheNode` that
///       will be declared later. This allows us to use it as if it was
///       declared, provided that only the real `TheNode` is used as this
///       argument.
template<typename Node,
         typename EdgeLabel,
         bool HasParent,
         size_t SmallSize,
         template<typename, typename, bool, size_t, typename, size_t, bool>
         class TheNode,
         typename FinalType,
         size_t ParentSmallSize,
         bool ParentHasEntryNode>
struct MutableEdgeNodeBaseTCalc {
  static constexpr bool
    NoDerivation = std::is_same_v<FinalType, std::false_type>;
  using NodeType = TheNode<Node,
                           EdgeLabel,
                           HasParent,
                           SmallSize,
                           FinalType,
                           ParentSmallSize,
                           ParentHasEntryNode>;
  using DerivedType = std::conditional_t<NoDerivation, NodeType, FinalType>;
  using GenericGraph = GenericGraph<DerivedType,
                                    ParentSmallSize,
                                    ParentHasEntryNode>;
  using ParentType = Parent<GenericGraph, Node>;
  using Result = std::conditional_t<HasParent, ParentType, Node>;
};

template<typename NodeType, typename LabelType>
struct OwningEdge {
  NodeType *Neighbor;
  std::unique_ptr<LabelType> Label;
};

template<typename NodeType, typename LabelType>
struct NonOwningEdge {
  NodeType *Neighbor;
  LabelType *Label;
};

template<typename NodeType, typename LabelType>
struct EdgeView {
  NodeType &Neighbor;
  LabelType &Label;

  explicit EdgeView(OwningEdge<NodeType, LabelType> &E) :
    Neighbor(*E.Neighbor), Label(*E.Label) {}
  explicit EdgeView(NonOwningEdge<NodeType, LabelType> &E) :
    Neighbor(*E.Neighbor), Label(*E.Label) {}
};

template<typename NodeType, typename LabelType>
struct ConstEdgeView {
  NodeType const &Neighbor;
  LabelType const &Label;

  explicit ConstEdgeView(OwningEdge<NodeType, LabelType> const &E) :
    Neighbor(*E.Neighbor), Label(*E.Label) {}
  explicit ConstEdgeView(NonOwningEdge<NodeType, LabelType> const &E) :
    Neighbor(*E.Neighbor), Label(*E.Label) {}
};

template<typename NodeType>
struct Unlabeled {
  NodeType *Neighbor;
};

template<typename NodeType>
struct UnlabeledView {
  NodeType &Neighbor;

  explicit UnlabeledView(Unlabeled<NodeType> &E) : Neighbor(*E.Neighbor) {}
};

template<typename NodeType>
struct ConstUnlabeledView {
  NodeType const &Neighbor;

  explicit ConstUnlabeledView(Unlabeled<NodeType> const &E) :
    Neighbor(*E.Neighbor) {}
};
} // namespace detail

/// A node type suitable for graphs where the edge labels are not cheap
/// to copy or need to be modified often.
template<typename Node,
         typename EdgeLabel = Empty,
         bool HasParent = true,
         size_t SmallSize = 2,
         typename FinalType = std::false_type,
         size_t ParentSmallSize = 16,
         bool ParentHasEntryNode = true>
class MutableEdgeNode
  : public detail::MutableEdgeNodeBaseTCalc<Node,
                                            EdgeLabel,
                                            HasParent,
                                            SmallSize,
                                            MutableEdgeNode,
                                            FinalType,
                                            ParentSmallSize,
                                            ParentHasEntryNode>::Result {
public:
  static constexpr bool is_mutable_edge_node = true;
  static constexpr bool has_parent = HasParent;
  using TypeCalc = detail::MutableEdgeNodeBaseTCalc<Node,
                                                    EdgeLabel,
                                                    HasParent,
                                                    SmallSize,
                                                    MutableEdgeNode,
                                                    FinalType,
                                                    ParentSmallSize,
                                                    ParentHasEntryNode>;
  using DerivedType = typename TypeCalc::DerivedType;
  using Base = typename TypeCalc::Result;
  using NodeData = Node;
  using EdgeLabelData = EdgeLabel;

  static constexpr bool AreEdgesLabeled = !std::is_same_v<EdgeLabel, Empty>;

private:
  template<bool _Cond, typename _Iftrue, typename _Iffalse>
  using C = std::conditional_t<_Cond, _Iftrue, _Iffalse>;

public:
  using Edge = EdgeLabel;
  using EdgeView = C<AreEdgesLabeled,
                     detail::EdgeView<DerivedType, EdgeLabel>,
                     detail::UnlabeledView<DerivedType>>;
  using ConstEdgeView = C<AreEdgesLabeled,
                          detail::ConstEdgeView<DerivedType, EdgeLabel>,
                          detail::ConstUnlabeledView<DerivedType>>;

protected:
  using LabeledOwningEdge = detail::OwningEdge<DerivedType, EdgeLabel>;
  using UnlabeledOwningEdge = detail::Unlabeled<DerivedType>;
  using LabeledNonOwningEdge = detail::NonOwningEdge<DerivedType, EdgeLabel>;
  using UnlabeledNonOwningEdge = detail::Unlabeled<DerivedType>;

public:
  using OwningEdge = C<AreEdgesLabeled, LabeledOwningEdge, UnlabeledOwningEdge>;
  using NonOwningEdge = C<AreEdgesLabeled,
                          LabeledNonOwningEdge,
                          UnlabeledNonOwningEdge>;
  using EdgeOwnerContainer = llvm::SmallVector<OwningEdge, SmallSize>;
  using EdgeViewContainer = llvm::SmallVector<NonOwningEdge, SmallSize>;

public:
  template<typename... Args>
  explicit MutableEdgeNode(Args &&...args) :
    Base(std::forward<Args>(args)...) {}

  MutableEdgeNode(const MutableEdgeNode &) = default;
  MutableEdgeNode(MutableEdgeNode &&) = default;
  MutableEdgeNode &operator=(const MutableEdgeNode &) = default;
  MutableEdgeNode &operator=(MutableEdgeNode &&) = default;

public:
  // This stuff is needed by the DominatorTree implementation
  void printAsOperand(llvm::raw_ostream &, bool) const { revng_abort(); }

public:
  EdgeView addSuccessor(MutableEdgeNode &NewSuccessor, EdgeLabel EL = {}) {
    revng_assert(!hasSuccessor(NewSuccessor),
                 "Only one edge is allowed between two nodes.");
    auto [Owner, View] = constructEdge(*this, NewSuccessor, std::move(EL));
    auto &Output = Successors.emplace_back(std::move(Owner));
    NewSuccessor.Predecessors.emplace_back(std::move(View));
    return EdgeView(Output);
  }
  EdgeView addPredecessor(MutableEdgeNode &NewPredecessor, EdgeLabel EL = {}) {
    revng_assert(!hasPredecessor(NewPredecessor),
                 "Only one edge is allowed between two nodes.");
    auto [Owner, View] = constructEdge(NewPredecessor, *this, std::move(EL));
    auto &Output = NewPredecessor.Successors.emplace_back(std::move(Owner));
    Predecessors.emplace_back(std::move(View));
    return EdgeView(Output);
  }

protected:
  struct SuccessorFilters {
    static EdgeView toView(OwningEdge &E) { return EdgeView(E); }
    static ConstEdgeView toConstView(OwningEdge const &E) {
      return ConstEdgeView(E);
    }

    static DerivedType *&toNeighbor(OwningEdge &E) { return E.Neighbor; }
    static DerivedType const *const &toConstNeighbor(OwningEdge const &E) {
      return E.Neighbor;
    }
  };

  struct PredecessorFilters {
    static EdgeView toView(NonOwningEdge &E) { return EdgeView(E); }
    static ConstEdgeView toConstView(NonOwningEdge const &E) {
      return ConstEdgeView(E);
    }

    static DerivedType *&toNeighbor(NonOwningEdge &E) { return E.Neighbor; }
    static DerivedType const *const &toConstNeighbor(NonOwningEdge const &E) {
      return E.Neighbor;
    }
  };

private:
  template<typename IteratorType, typename FunctionType>
  using mapped = revng::mapped_iterator<IteratorType, FunctionType>;

  using SuccPointer = OwningEdge *;
  using CSuccPointer = const OwningEdge *;
  using PredPointer = NonOwningEdge *;
  using CPredPointer = const NonOwningEdge *;

  using SuccV = std::decay_t<decltype(SuccessorFilters::toView)>;
  using CSuccV = std::decay_t<decltype(SuccessorFilters::toConstView)>;
  using SuccN = std::decay_t<decltype(SuccessorFilters::toNeighbor)>;
  using CSuccN = std::decay_t<decltype(SuccessorFilters::toConstNeighbor)>;
  using PredV = std::decay_t<decltype(PredecessorFilters::toView)>;
  using CPredV = std::decay_t<decltype(PredecessorFilters::toConstView)>;
  using PredN = std::decay_t<decltype(PredecessorFilters::toNeighbor)>;
  using CPredN = std::decay_t<decltype(PredecessorFilters::toConstNeighbor)>;

public:
  using SuccessorEdgeIterator = mapped<SuccPointer, SuccV>;
  using ConstSuccessorEdgeIterator = mapped<CSuccPointer, CSuccV>;
  using SuccessorIterator = mapped<SuccPointer, SuccN>;
  using ConstSuccessorIterator = mapped<CSuccPointer, CSuccN>;

  using PredecessorEdgeIterator = mapped<PredPointer, PredV>;
  using ConstPredecessorEdgeIterator = mapped<CPredPointer, CPredV>;
  using PredecessorIterator = mapped<PredPointer, PredN>;
  using ConstPredecessorIterator = mapped<CPredPointer, CPredN>;

public:
  llvm::iterator_range<SuccessorEdgeIterator> successor_edges() {
    auto Range = llvm::make_range(Successors.begin(), Successors.end());
    return revng::map_range(Range, SuccessorFilters::toView);
  }
  llvm::iterator_range<ConstSuccessorEdgeIterator> successor_edges() const {
    auto Range = llvm::make_range(Successors.begin(), Successors.end());
    return revng::map_range(Range, SuccessorFilters::toConstView);
  }
  llvm::iterator_range<SuccessorIterator> successors() {
    auto Range = llvm::make_range(Successors.begin(), Successors.end());
    return revng::map_range(Range, SuccessorFilters::toNeighbor);
  }
  llvm::iterator_range<ConstSuccessorIterator> successors() const {
    auto Range = llvm::make_range(Successors.begin(), Successors.end());
    return revng::map_range(Range, SuccessorFilters::toConstNeighbor);
  }

  llvm::iterator_range<PredecessorEdgeIterator> predecessor_edges() {
    auto Range = llvm::make_range(Predecessors.begin(), Predecessors.end());
    return revng::map_range(Range, PredecessorFilters::toView);
  }
  llvm::iterator_range<ConstPredecessorEdgeIterator> predecessor_edges() const {
    auto Range = llvm::make_range(Predecessors.begin(), Predecessors.end());
    return revng::map_range(Range, PredecessorFilters::toConstView);
  }
  llvm::iterator_range<PredecessorIterator> predecessors() {
    auto Range = llvm::make_range(Predecessors.begin(), Predecessors.end());
    return revng::map_range(Range, PredecessorFilters::toNeighbor);
  }
  llvm::iterator_range<ConstPredecessorIterator> predecessors() const {
    auto Range = llvm::make_range(Predecessors.begin(), Predecessors.end());
    return revng::map_range(Range, PredecessorFilters::toConstNeighbor);
  }

private:
  auto findSuccessorImpl(DerivedType const &S) {
    auto Comparator = [&S](auto &Edge) { return Edge.Neighbor == &S; };
    return std::find_if(Successors.begin(), Successors.end(), Comparator);
  }
  auto findSuccessorImpl(DerivedType const &S) const {
    auto Comparator = [&S](auto const &Edge) { return Edge.Neighbor == &S; };
    return std::find_if(Successors.begin(), Successors.end(), Comparator);
  }
  auto findPredecessorImpl(DerivedType const &P) {
    auto Comparator = [&P](auto &Edge) { return Edge.Neighbor == &P; };
    return std::find_if(Predecessors.begin(), Predecessors.end(), Comparator);
  }
  auto findPredecessorImpl(DerivedType const &P) const {
    auto Comparator = [&P](auto const &Edge) { return Edge.Neighbor == &P; };
    return std::find_if(Predecessors.begin(), Predecessors.end(), Comparator);
  }

public:
  SuccessorEdgeIterator findSuccessorEdge(DerivedType const &S) {
    return SuccessorEdgeIterator(findSuccessorImpl(S),
                                 SuccessorFilters::toView);
  }
  ConstSuccessorEdgeIterator findSuccessorEdge(DerivedType const &S) const {
    return ConstSuccessorEdgeIterator(findSuccessorImpl(S),
                                      SuccessorFilters::toConstView);
  }
  PredecessorEdgeIterator findPredecessorEdge(DerivedType const &S) {
    return PredecessorEdgeIterator(findPredecessorImpl(S),
                                   PredecessorFilters::toView);
  }
  ConstPredecessorEdgeIterator findPredecessorEdge(DerivedType const &S) const {
    return ConstPredecessorEdgeIterator(findPredecessorImpl(S),
                                        PredecessorFilters::toConstView);
  }

  SuccessorIterator findSuccessor(DerivedType const &S) {
    return SuccessorIterator(findSuccessorImpl(S),
                             SuccessorFilters::toNeighbor);
  }
  ConstSuccessorIterator findSuccessor(DerivedType const &S) const {
    return ConstSuccessorIterator(findSuccessorImpl(S),
                                  SuccessorFilters::toConstNeighbor);
  }
  PredecessorIterator findPredecessor(DerivedType const &S) {
    return PredecessorIterator(findPredecessorImpl(S),
                               PredecessorFilters::toNeighbor);
  }
  ConstPredecessorIterator findPredecessor(DerivedType const &S) const {
    return ConstPredecessorIterator(findPredecessorImpl(S),
                                    PredecessorFilters::toConstNeighbor);
  }

public:
  bool hasSuccessor(DerivedType const &S) const {
    return findSuccessorImpl(S) != Successors.end();
  }
  bool hasPredecessor(DerivedType const &S) const {
    return findPredecessorImpl(S) != Predecessors.end();
  }

public:
  size_t successorCount() const { return Successors.size(); }
  size_t predecessorCount() const { return Predecessors.size(); }

  bool hasSuccessors() const { return Successors.size() != 0; }
  bool hasPredecessors() const { return Predecessors.size() != 0; }

protected:
  using OwnerIteratorImpl = typename EdgeOwnerContainer::const_iterator;
  using ViewIteratorImpl = typename EdgeViewContainer::const_iterator;

  auto removeSuccessorImpl(OwnerIteratorImpl InputIterator) {
    if (Successors.empty())
      return Successors.end();

    auto Iterator = Successors.begin();
    std::advance(Iterator,
                 std::distance<OwnerIteratorImpl>(Iterator, InputIterator));

    // Maybe we should do some extra checks as to whether `Iterator` is valid.
    auto *Successor = Iterator->Neighbor;
    if (Successor->Predecessors.empty())
      return Iterator;

    auto PredecessorIt = Successor->findPredecessorImpl(*this);
    revng_assert(PredecessorIt != Successor->Predecessors.end(),
                 "Half of an edge is missing, graph layout is broken.");
    std::swap(*PredecessorIt, Successor->Predecessors.back());
    Successor->Predecessors.pop_back();

    std::swap(*Iterator, Successors.back());
    Successors.pop_back();

    return Iterator;
  }
  auto removePredecessorImpl(ViewIteratorImpl InputIterator) {
    if (Predecessors.empty())
      return Predecessors.end();

    auto Iterator = Predecessors.begin();
    std::advance(Iterator,
                 std::distance<ViewIteratorImpl>(Iterator, InputIterator));

    auto *Predecessor = Iterator->Neighbor;
    if (Predecessor->Successors.empty())
      return Iterator;

    // Maybe we should do some extra checks as to whether `Iterator` is valid.
    auto SuccessorIt = Predecessor->findSuccessorImpl(*this);
    revng_assert(SuccessorIt != Predecessor->Successors.end(),
                 "Half of an edge is missing, graph layout is broken.");
    std::swap(*SuccessorIt, Predecessor->Successors.back());
    Predecessor->Successors.pop_back();

    std::swap(*Iterator, Predecessors.back());
    Predecessors.pop_back();

    return Iterator;
  }

public:
  auto removeSuccessor(ConstSuccessorEdgeIterator Iterator) {
    auto Result = removeSuccessorImpl(Iterator.getCurrent());
    return SuccessorEdgeIterator(Result, SuccessorFilters::toView);
  }
  auto removeSuccessor(ConstSuccessorIterator Iterator) {
    auto Result = removeSuccessorImpl(Iterator.getCurrent());
    return SuccessorIterator(Result, SuccessorFilters::toNeighbor);
  }
  auto removeSuccessor(DerivedType const &S) {
    auto Result = removeSuccessorImpl(findSuccessorImpl(S));
    return SuccessorIterator(Result, SuccessorFilters::toNeighbor);
  }

  auto removePredecessor(ConstPredecessorEdgeIterator Iterator) {
    auto Result = removePredecessorImpl(Iterator.getCurrent());
    return ReverseSuccessorEdgeIterator(Result, PredecessorFilters::toView);
  }
  auto removePredecessor(ConstPredecessorIterator Iterator) {
    auto Result = removePredecessorImpl(Iterator.getCurrent());
    return ReverseSuccessorEdgeIterator(Result, PredecessorFilters::toNeighbor);
  }
  auto removePredecessor(DerivedType const &P) {
    auto Result = removePredecessorImpl(findPredecessorImpl(P));
    return ReverseSuccessorEdgeIterator(Result, PredecessorFilters::toNeighbor);
  }

public:
  auto removeSuccessor(SuccessorEdgeIterator Iterator) {
    auto Converted = ConstSuccessorEdgeIterator(Iterator.getCurrent(),
                                                SuccessorFilters::toConstView);
    return removeSuccessor(Converted);
  }
  auto removeSuccessor(SuccessorIterator Iterator) {
    auto Converted = ConstSuccessorIterator(Iterator.getCurrent(),
                                            SuccessorFilters::toConstNeighbor);
    return removeSuccessor(Converted);
  }
  auto removePredecessor(PredecessorEdgeIterator Iterator) {
    auto &F = PredecessorFilters::toConstView;
    auto Converted = ConstPredecessorEdgeIterator(Iterator.getCurrent(), F);
    return removePredecessor(Converted);
  }
  auto removePredecessor(PredecessorIterator Iterator) {
    auto Conv = ConstPredecessorIterator(Iterator.getCurrent(),
                                         PredecessorFilters::toConstNeighbor);
    return removePredecessor(Conv);
  }

public:
  MutableEdgeNode &disconnect() {
    for (auto It = Successors.begin(); It != Successors.end();)
      It = removeSuccessorImpl(It);
    for (auto It = Predecessors.begin(); It != Predecessors.end();)
      It = removePredecessorImpl(It);
    return *this;
  }

protected:
  std::tuple<OwningEdge, NonOwningEdge>
  constructEdge(MutableEdgeNode &From, MutableEdgeNode &To, EdgeLabel &&EL) {
    if constexpr (AreEdgesLabeled) {
      LabeledOwningEdge O{ &To, std::make_unique<EdgeLabel>(std::move(EL)) };
      LabeledNonOwningEdge V{ &From, O.Label.get() };
      return { std::move(O), std::move(V) };
    } else {
      return { UnlabeledOwningEdge{ &To }, UnlabeledNonOwningEdge{ &From } };
    }
  }

private:
  EdgeOwnerContainer Successors;
  EdgeViewContainer Predecessors;
};

/// Simple data structure to hold the EntryNode of a GenericGraph
template<typename NodeT>
class EntryNode {
private:
  NodeT *EntryNode = nullptr;

public:
  NodeT *getEntryNode() const { return EntryNode; }
  void setEntryNode(NodeT *EntryNode) { this->EntryNode = EntryNode; }
};

/// Generic graph parametrized in the node type
///
/// This graph owns its nodes (but not the edges).
/// It can optionally have an elected entry point.
template<typename NodeT, size_t SmallSize, bool HasEntryNode>
class GenericGraph
  : public std::conditional_t<HasEntryNode, EntryNode<NodeT>, Empty> {
public:
  static const bool is_generic_graph = true;
  using NodesContainer = llvm::SmallVector<std::unique_ptr<NodeT>, SmallSize>;
  using Node = NodeT;
  static constexpr bool hasEntryNode = HasEntryNode;

private:
  using nodes_iterator_impl = typename NodesContainer::iterator;
  using const_nodes_iterator_impl = typename NodesContainer::const_iterator;

public:
  static NodeT *getNode(std::unique_ptr<NodeT> &E) { return E.get(); }
  static const NodeT *getConstNode(const std::unique_ptr<NodeT> &E) {
    return E.get();
  }

  // TODO: these iterators will not work with llvm::filter_iterator,
  //       since the mapped type is not a reference
  using nodes_iterator = llvm::mapped_iterator<nodes_iterator_impl,
                                               decltype(&getNode)>;
  using const_nodes_iterator = llvm::mapped_iterator<const_nodes_iterator_impl,
                                                     decltype(&getConstNode)>;

  llvm::iterator_range<nodes_iterator> nodes() {
    return llvm::map_range(llvm::make_range(Nodes.begin(), Nodes.end()),
                           getNode);
  }

  llvm::iterator_range<const_nodes_iterator> nodes() const {
    return llvm::map_range(llvm::make_range(Nodes.begin(), Nodes.end()),
                           getConstNode);
  }

  size_t size() const { return Nodes.size(); }

public:
  nodes_iterator findNode(Node const *NodePtr) {
    auto Comparator = [&NodePtr](auto &N) { return N.get() == NodePtr; };
    auto InternalIt = std::find_if(Nodes.begin(), Nodes.end(), Comparator);
    return nodes_iterator(InternalIt, getNode);
  }
  const_nodes_iterator findNode(Node const *NodePtr) const {
    auto Comparator = [&NodePtr](auto &N) { return N.get() == NodePtr; };
    auto InternalIt = std::find_if(Nodes.begin(), Nodes.end(), Comparator);
    return nodes_iterator(InternalIt, getConstNode);
  }

public:
  bool hasNodes() const { return Nodes.size() != 0; }
  bool hasNode(Node const *NodePtr) const {
    return findNode(NodePtr) != Nodes.end();
  }

public:
  NodeT *addNode(std::unique_ptr<NodeT> &&Ptr) {
    Nodes.emplace_back(std::move(Ptr));
    if constexpr (NodeT::has_parent)
      Nodes.back()->setParent(this);
    return Nodes.back().get();
  }

  template<class... Args>
  NodeT *addNode(Args &&...A) {
    Nodes.push_back(std::make_unique<NodeT>(std::forward<Args>(A)...));
    if constexpr (NodeT::has_parent)
      Nodes.back()->setParent(this);
    return Nodes.back().get();
  }

  nodes_iterator removeNode(nodes_iterator It) {
    if constexpr (IsMutableEdgeNode<Node>)
      (*It.getCurrent())->disconnect();

    auto InternalIt = Nodes.erase(It.getCurrent());
    return nodes_iterator(InternalIt, getNode);
  }
  nodes_iterator removeNode(Node const *NodePtr) {
    return removeNode(findNode(NodePtr));
  }

private:
  NodesContainer Nodes;
};

//
// GraphTraits implementation for GenericGraph
//
namespace llvm {

/// Implement GraphTraits<ForwardNode>
template<IsForwardNode T>
struct GraphTraits<T *> {
public:
  using NodeRef = T *;
  using ChildIteratorType = std::conditional_t<std::is_const_v<T>,
                                               typename T::const_child_iterator,
                                               typename T::child_iterator>;

  using EdgeRef = typename T::Edge &;
  template<typename Ty, typename True, typename False>
  using if_const = std::conditional_t<std::is_const_v<Ty>, True, False>;
  using ChildEdgeIteratorType = if_const<T,
                                         typename T::const_edge_iterator,
                                         typename T::edge_iterator>;

public:
  static ChildIteratorType child_begin(NodeRef N) {
    return N->successors().begin();
  }

  static ChildIteratorType child_end(NodeRef N) {
    return N->successors().end();
  }

  static ChildEdgeIteratorType child_edge_begin(NodeRef N) {
    return N->successor_edges().begin();
  }

  static ChildEdgeIteratorType child_edge_end(NodeRef N) {
    return N->successor_edges().end();
  }

  static NodeRef edge_dest(EdgeRef Edge) { return Edge.Neighbor; }

  static NodeRef getEntryNode(NodeRef N) { return N; };
};

/// Implement GraphTraits<MutableEdgeNode>
template<IsMutableEdgeNode T>
struct GraphTraits<T *> {
public:
  using NodeRef = T *;
  using EdgeRef = typename T::EdgeView;

private:
  using ChildNodeIt = decltype(std::declval<T>().successors().begin());
  using ChildEdgeIt = decltype(std::declval<T>().successor_edges().begin());

public:
  using ChildIteratorType = ChildNodeIt;
  using ChildEdgeIteratorType = ChildEdgeIt;

public:
  static auto child_begin(T *N) { return N->successors().begin(); }
  static auto child_end(T *N) { return N->successors().end(); }

  static auto child_edge_begin(T *N) { return N->successor_edges().begin(); }
  static auto child_edge_end(T *N) { return N->successor_edges().end(); }

  static T *edge_dest(EdgeRef Edge) { return &Edge.Neighbor; }
  static T *getEntryNode(T *N) { return N; };
};

/// Implement GraphTraits<Inverse<MutableEdgeNode>>
template<IsMutableEdgeNode T>
struct GraphTraits<llvm::Inverse<T *>> {
public:
  using NodeRef = T *;
  using EdgeRef = typename T::EdgeView;

private:
  using ChildNodeIt = decltype(std::declval<T>().predecessors().begin());
  using ChildEdgeIt = decltype(std::declval<T>().predecessor_edges().begin());

public:
  using ChildIteratorType = ChildNodeIt;
  using ChildEdgeIteratorType = ChildEdgeIt;

public:
  static auto child_begin(T *N) { return N->predecessors().begin(); }
  static auto child_end(T *N) { return N->predecessors().end(); }

  static auto child_edge_begin(T *N) { return N->predecessor_edges().begin(); }
  static auto child_edge_end(T *N) { return N->predecessor_edges().end(); }

  static T *edge_dest(EdgeRef Edge) { return &Edge.Neighbor; }
  static T *getEntryNode(llvm::Inverse<T *> N) { return N.Graph; };
};

/// Implement GraphTraits<GenericGraph>
template<IsGenericGraph T>
struct GraphTraits<T *> : public GraphTraits<typename T::Node *> {

  using NodeRef = std::conditional_t<std::is_const_v<T>,
                                     const typename T::Node *,
                                     typename T::Node *>;
  using nodes_iterator = std::conditional_t<std::is_const_v<T>,
                                            typename T::const_nodes_iterator,
                                            typename T::nodes_iterator>;

  static NodeRef getEntryNode(T *G) { return G->getEntryNode(); }

  static nodes_iterator nodes_begin(T *G) { return G->nodes().begin(); }

  static nodes_iterator nodes_end(T *G) { return G->nodes().end(); }

  static size_t size(T *G) { return G->size(); }
};

/// Implement GraphTraits<Inverse<BidirectionalNode>>
template<IsBidirectionalNode T>
struct GraphTraits<llvm::Inverse<T *>> {
public:
  using NodeRef = T *;
  using ChildIteratorType = std::conditional_t<std::is_const_v<T>,
                                               typename T::const_child_iterator,
                                               typename T::child_iterator>;

public:
  static ChildIteratorType child_begin(NodeRef N) {
    return N->predecessors().begin();
  }

  static ChildIteratorType child_end(NodeRef N) {
    return N->predecessors().end();
  }

  static NodeRef getEntryNode(llvm::Inverse<NodeRef> N) { return N.Graph; };
};

} // namespace llvm
