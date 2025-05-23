/** \file
 * \brief Implementation of class PlanRepExpansion.
 *
 * \author Carsten Gutwenger
 *
 * \par License:
 * This file is part of the Open Graph Drawing Framework (OGDF).
 *
 * \par
 * Copyright (C)<br>
 * See README.md in the OGDF root directory for details.
 *
 * \par
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * Version 2 or 3 as published by the Free Software Foundation;
 * see the file LICENSE.txt included in the packaging of this file
 * for details.
 *
 * \par
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * \par
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, see
 * http://www.gnu.org/copyleft/gpl.html
 */


#include <ogdf/basic/Array.h>
#include <ogdf/basic/CombinatorialEmbedding.h>
#include <ogdf/basic/FaceSet.h>
#include <ogdf/basic/Graph.h>
#include <ogdf/basic/GraphList.h>
#include <ogdf/basic/GraphSets.h>
#include <ogdf/basic/List.h>
#include <ogdf/basic/SList.h>
#include <ogdf/basic/basic.h>
#include <ogdf/basic/extended_graph_alg.h>
#include <ogdf/basic/simple_graph_alg.h>
#include <ogdf/basic/tuples.h>
#include <ogdf/planarity/PlanRepExpansion.h>

#include <utility>

namespace ogdf {

PlanRepExpansion::PlanRepExpansion(const Graph& G) {
	List<node> splittableNodes;
	for (node v : G.nodes) {
		if (v->degree() >= 4) {
			splittableNodes.pushBack(v);
		}
	}

	doInit(G, splittableNodes);
}

PlanRepExpansion::PlanRepExpansion(const Graph& G, const List<node>& splittableNodes) {
	doInit(G, splittableNodes);
}

void PlanRepExpansion::doInit(const Graph& G, const List<node>& splittableNodes) {
	m_pGraph = &G;
	m_eAuxCopy.init(G);

	// compute connected component of G
	NodeArray<int> component(G);
	m_numCC = connectedComponents(G, component);

	// intialize the array of lists of nodes contained in a CC
	m_nodesInCC.init(m_numCC);

	for (node v : G.nodes) {
		m_nodesInCC[component[v]].pushBack(v);
	}

	m_currentCC = -1; // not yet initialized

	m_vCopy.init(G);
	m_eCopy.init(G);
	m_vOrig.init(*this, nullptr);
	m_eOrig.init(*this, nullptr);
	m_vIterator.init(*this, nullptr);
	m_eIterator.init(*this, nullptr);

	m_splittable.init(*this, false);
	m_splittableOrig.init(G, false);
	m_eNodeSplit.init(*this, nullptr);

	ListConstIterator<node> it;
	for (it = splittableNodes.begin(); it.valid(); ++it) {
		if ((*it)->degree() >= 4) {
			m_splittableOrig[*it] = true;
		}
	}
}

void PlanRepExpansion::initCC(int i) {
	// delete copy / chain fields for originals of nodes in current cc
	// (since we remove all these copies in initByNodes(...)
	if (m_currentCC >= 0) {
		const List<node>& origInCC = nodesInCC(i);
		ListConstIterator<node> itV;

		for (itV = origInCC.begin(); itV.valid(); ++itV) {
			node vG = *itV;

			m_vCopy[vG].clear();

			for (adjEntry adj : vG->adjEntries) {
				if ((adj->index() & 1) == 0) {
					continue;
				}
				edge eG = adj->theEdge();

				m_eCopy[eG].clear();
			}
		}
	}

	m_currentCC = i;

	NodeArray<node> vCopy(*m_pGraph);
	m_eAuxCopy.init(*m_pGraph);
	insert(m_nodesInCC[i], m_pGraph->edges, vCopy, m_eAuxCopy);

	ListConstIterator<node> itV;
	for (itV = m_nodesInCC[i].begin(); itV.valid(); ++itV) {
		node vOrig = *itV;
		node v = vCopy[vOrig];

		m_vOrig[v] = vOrig;
		m_vIterator[v] = m_vCopy[vOrig].pushBack(v);
		m_splittable[v] = m_splittableOrig[vOrig];

		for (adjEntry adj : vOrig->adjEntries) {
			if ((adj->index() & 1) == 0) {
				edge e = adj->theEdge();
				m_eIterator[m_eAuxCopy[e]] = m_eCopy[e].pushBack(m_eAuxCopy[e]);
				m_eOrig[m_eAuxCopy[e]] = e;
			}
		}
	}

	m_nodeSplits.clear();
}

void PlanRepExpansion::delEdge(edge e) {
	edge eOrig = m_eOrig[e];
	OGDF_ASSERT(m_eCopy[eOrig].size() == 1);
	Graph::delEdge(e);
	m_eCopy[eOrig].clear();
}

bool PlanRepExpansion::embed() { return planarEmbed(*this); }

void PlanRepExpansion::prepareNodeSplit(const SList<adjEntry>& partitionLeft, adjEntry& adjLeft,
		adjEntry& adjRight) {
	OGDF_ASSERT(!partitionLeft.empty());
	OGDF_ASSERT(partitionLeft.front()->theNode()->degree() > partitionLeft.size());

	SListConstIterator<adjEntry> it = partitionLeft.begin();
	adjEntry adj = *it;
	adjLeft = adj;

	for (++it; it.valid(); ++it) {
		moveAdjAfter(*it, adj);
		adj = *it;
	}

	adjRight = adj->cyclicSucc();
}

void PlanRepExpansion::insertEdgePath(edge eOrig, nodeSplit ns, node vStart, node vEnd,
		List<Crossing>& eip, edge eSrc, edge eTgt) {
	OGDF_ASSERT((eOrig != nullptr && ns == nullptr) || (eOrig == nullptr && ns != nullptr));

	if (eOrig) {
		m_eCopy[eOrig].clear();
	} else {
		ns->m_path.clear();
	}

	if (eSrc != nullptr) {
		if (eOrig) {
			m_eIterator[eSrc] = m_eCopy[eOrig].pushBack(eSrc);
			m_eOrig[eSrc] = eOrig;
		} else {
			m_eIterator[eSrc] = ns->m_path.pushBack(eSrc);
			m_eNodeSplit[eSrc] = ns;
		}
	}

	node v = vStart;
	ListConstIterator<Crossing> it;
	for (it = eip.begin(); it.valid(); ++it) {
		adjEntry adj = (*it).m_adj;
		if (adj == nullptr) {
			adjEntry adjLeft, adjRight;
			prepareNodeSplit((*it).m_partitionLeft, adjLeft, adjRight);

			node w = splitNode(adjLeft, adjRight);
			edge eNew = adjLeft->cyclicPred()->theEdge();

			m_vIterator[w] = m_vCopy[m_vOrig[adjLeft->theNode()]].pushBack(w);
			m_splittable[w] = true;
			m_vOrig[w] = m_vOrig[adjLeft->theNode()];

			ListIterator<NodeSplit> itNS = m_nodeSplits.pushBack(NodeSplit());
			(*itNS).m_nsIterator = itNS;
			m_eIterator[eNew] = (*itNS).m_path.pushBack(eNew);
			m_eNodeSplit[eNew] = &(*itNS);

			adj = adjRight->cyclicPred();
		}

		node u = split(adj->theEdge())->source();
		edge eNew = newEdge(v, u);

		if (eOrig) {
			m_eIterator[eNew] = m_eCopy[eOrig].pushBack(eNew);
			m_eOrig[eNew] = eOrig;
		} else {
			m_eIterator[eNew] = ns->m_path.pushBack(eNew);
			m_eNodeSplit[eNew] = ns;
		}

		v = u;
	}

	edge eNew = newEdge(v, vEnd);
	if (eOrig) {
		m_eIterator[eNew] = m_eCopy[eOrig].pushBack(eNew);
		m_eOrig[eNew] = eOrig;
	} else {
		m_eIterator[eNew] = ns->m_path.pushBack(eNew);
		m_eNodeSplit[eNew] = ns;
	}

	if (eTgt != nullptr) {
		if (eOrig) {
			m_eIterator[eTgt] = m_eCopy[eOrig].pushBack(eTgt);
			m_eOrig[eTgt] = eOrig;
		} else {
			m_eIterator[eTgt] = ns->m_path.pushBack(eTgt);
			m_eNodeSplit[eTgt] = ns;
		}
	}
}

void PlanRepExpansion::insertEdgePathEmbedded(edge eOrig, nodeSplit ns, CombinatorialEmbedding& E,
		const List<Tuple2<adjEntry, adjEntry>>& crossedEdges) {
	OGDF_ASSERT((eOrig != nullptr && ns == nullptr) || (eOrig == nullptr && ns != nullptr));

	if (eOrig) {
		m_eCopy[eOrig].clear();
	} else {
		ns->m_path.clear();
	}

	adjEntry adjSrc, adjTgt;
	ListConstIterator<Tuple2<adjEntry, adjEntry>> it = crossedEdges.begin();

	// iterate over all adjacency entries in crossedEdges except for first
	// and last
	adjSrc = (*it).x1();
	for (++it; it.valid() && it.succ().valid(); ++it) {
		adjEntry adj = (*it).x1();
		adjEntry adj2 = (*it).x2();

		if (adj2 != nullptr) {
			OGDF_ASSERT(adj->theNode() == adj2->theNode());
			OGDF_ASSERT(E.rightFace(adjSrc) == E.rightFace(adj->twin()));
			node w = E.splitNode(adj, adj2);
			edge eNew = adj->cyclicPred()->theEdge();

			m_vIterator[w] = m_vCopy[m_vOrig[adj->theNode()]].pushBack(w);
			m_splittable[w] = true;
			m_vOrig[w] = m_vOrig[adj->theNode()];

			ListIterator<NodeSplit> itNS = m_nodeSplits.pushBack(NodeSplit());
			(*itNS).m_nsIterator = itNS;
			m_eIterator[eNew] = (*itNS).m_path.pushBack(eNew);
			m_eNodeSplit[eNew] = &(*itNS);

			adj = adj2->cyclicPred();
		}

		// split edge
		node u = E.split(adj->theEdge())->source();

		// determine target adjacency entry and source adjacency entry
		// in the next iteration step
		adjTgt = u->firstAdj();
		adjEntry adjSrcNext = adjTgt->succ();

		if (adjTgt != adj->twin()) {
			std::swap(adjTgt, adjSrcNext);
		}

		OGDF_ASSERT(adjTgt == adj->twin());

		// insert a new edge into the face
		edge eNew = E.splitFace(adjSrc, adjTgt);

		if (eOrig) {
			m_eIterator[eNew] = m_eCopy[eOrig].pushBack(eNew);
			m_eOrig[eNew] = eOrig;
		} else {
			m_eIterator[eNew] = ns->m_path.pushBack(eNew);
			m_eNodeSplit[eNew] = ns;
		}

		adjSrc = adjSrcNext;
	}

	// insert last edge
	edge eNew = E.splitFace(adjSrc, (*it).x1());

	if (eOrig) {
		m_eIterator[eNew] = m_eCopy[eOrig].pushBack(eNew);
		m_eOrig[eNew] = eOrig;
	} else {
		m_eIterator[eNew] = ns->m_path.pushBack(eNew);
		m_eNodeSplit[eNew] = ns;
	}
}

void PlanRepExpansion::removeEdgePathEmbedded(CombinatorialEmbedding& E, edge eOrig, nodeSplit ns,
		FaceSet& newFaces, NodeSet& mergedNodes, node& oldSrc, node& oldTgt) {
	OGDF_ASSERT((eOrig != nullptr && ns == nullptr) || (eOrig == nullptr && ns != nullptr));

	const List<edge>& path = (eOrig) ? m_eCopy[eOrig] : ns->m_path;
	ListConstIterator<edge> it = path.begin();

	oldSrc = path.front()->source();
	oldTgt = path.back()->target();

	newFaces.insert(E.joinFaces(*it));

	for (++it; it.valid(); ++it) {
		edge e = *it;
		node u = e->source();

		newFaces.remove(E.rightFace(e->adjSource()));
		newFaces.remove(E.rightFace(e->adjTarget()));

		newFaces.insert(E.joinFaces(e));

		edge eIn = u->firstAdj()->theEdge();
		edge eOut = u->lastAdj()->theEdge();
		if (eIn->target() != u) {
			std::swap(eIn, eOut);
		}

		E.unsplit(eIn, eOut);

		u = eIn->source();
		node v = eIn->target();

		node vOrig = m_vOrig[v];
		if (vOrig != nullptr && m_vOrig[u] == vOrig) {
			m_vCopy[vOrig].del(m_vIterator[v]);
			ListIterator<NodeSplit> itNS = m_eNodeSplit[eIn]->m_nsIterator;
			m_nodeSplits.del(itNS);

			E.contract(eIn);

			if (mergedNodes.isMember(v)) {
				mergedNodes.remove(v);
			}
			mergedNodes.insert(u);

			if (oldSrc == v) {
				oldSrc = u;
			}
			if (oldTgt == v) {
				oldTgt = u;
			}
		}
	}

	if (eOrig) {
		m_eCopy[eOrig].clear();
	} else {
		ns->m_path.clear();
	}
}

void PlanRepExpansion::removeEdgePath(edge eOrig, nodeSplit ns, node& oldSrc, node& oldTgt) {
	OGDF_ASSERT((eOrig != nullptr && ns == nullptr) || (eOrig == nullptr && ns != nullptr));

	const List<edge>& path = (eOrig) ? m_eCopy[eOrig] : ns->m_path;
	ListConstIterator<edge> it = path.begin();

	oldSrc = path.front()->source();
	oldTgt = path.back()->target();

	delEdge(*it);

	for (++it; it.valid(); ++it) {
		edge e = *it;
		node u = e->source();

		delEdge(e);

		edge eIn = u->firstAdj()->theEdge();
		edge eOut = u->lastAdj()->theEdge();
		if (eIn->target() != u) {
			std::swap(eIn, eOut);
		}

		unsplit(eIn, eOut);

		u = eIn->source();
		node v = eIn->target();

		node vOrig = m_vOrig[v];
		if (vOrig != nullptr && m_vOrig[u] == vOrig) {
			m_vCopy[vOrig].del(m_vIterator[v]);
			ListIterator<NodeSplit> itNS = m_eNodeSplit[eIn]->m_nsIterator;
			m_nodeSplits.del(itNS);

			contract(eIn);

			if (oldSrc == v) {
				oldSrc = u;
			}
			if (oldTgt == v) {
				oldTgt = u;
			}
		}
	}

	if (eOrig) {
		m_eCopy[eOrig].clear();
	} else {
		ns->m_path.clear();
	}
}

void PlanRepExpansion::contractSplit(nodeSplit ns, CombinatorialEmbedding& E) {
	OGDF_ASSERT(ns->m_path.size() == 1);

	edge e = ns->m_path.front();
	node v = e->target();
	node vOrig = m_vOrig[v];

	m_vCopy[vOrig].del(m_vIterator[v]);
	ListIterator<NodeSplit> itNS = ns->m_nsIterator;
	m_nodeSplits.del(itNS);

	E.contract(e);
}

void PlanRepExpansion::contractSplit(nodeSplit ns) {
	OGDF_ASSERT(ns->m_path.size() == 1);

	edge e = ns->m_path.front();
	node v = e->target();
	node vOrig = m_vOrig[v];

	m_vCopy[vOrig].del(m_vIterator[v]);
	ListIterator<NodeSplit> itNS = ns->m_nsIterator;
	m_nodeSplits.del(itNS);

	contract(e);
}

int PlanRepExpansion::computeNumberOfCrossings() const {
	int cr = 0;

	for (node v : nodes) {
		if (m_vOrig[v] == nullptr) {
			++cr;
		}
	}

	return cr;
}

edge PlanRepExpansion::split(edge e) {
	edge eNew = Graph::split(e);
	edge eOrig = m_eOrig[e];
	NodeSplit* ns = m_eNodeSplit[e];

	if ((m_eOrig[eNew] = eOrig) != nullptr) {
		m_eIterator[eNew] = m_eCopy[eOrig].insert(eNew, m_eIterator[e], Direction::after);

	} else if ((m_eNodeSplit[eNew] = ns) != nullptr) {
		m_eIterator[eNew] = ns->m_path.insert(eNew, m_eIterator[e], Direction::after);
	}

	return eNew;
}

void PlanRepExpansion::unsplit(edge eIn, edge eOut) {
	edge eOrig = m_eOrig[eOut];
	NodeSplit* ns = m_eNodeSplit[eOut];

	// update chain of eOrig if eOrig exists
	if (eOrig != nullptr) {
		m_eCopy[eOrig].del(m_eIterator[eOut]);

	} else if (ns != nullptr) {
		ns->m_path.del(m_eIterator[eOut]);
	}

	Graph::unsplit(eIn, eOut);
}

edge PlanRepExpansion::unsplitExpandNode(node u, edge eContract, edge eExpand,
		CombinatorialEmbedding& E) {
	NodeSplit* ns = m_eNodeSplit[eContract];
	List<edge>& path = ns->m_path;

	NodeSplit* nsExp = m_eNodeSplit[eExpand];
	edge eOrigExp = m_eOrig[eExpand];
	List<edge>& pathExp = (nsExp != nullptr) ? nsExp->m_path : m_eCopy[eOrigExp];

	if ((eExpand->target() == u && eContract->source() != u)
			|| (eExpand->source() == u && eContract->target() != u)) {
		// reverse path of eContract
		ListConstIterator<edge> it;
		for (it = path.begin(); it.valid(); ++it) {
			E.reverseEdge(*it);
		}
		path.reverse();
	}

	// remove u from list of copy nodes of its original
	m_vCopy[m_vOrig[u]].del(m_vIterator[u]);

	// unsplit u and enlarge edge path of eOrigExp
	edge eRet;
	if (eExpand->target() == u) {
		eRet = eExpand;
		E.unsplit(eExpand, eContract);

		ListConstIterator<edge> it;
		for (it = path.begin(); it.valid(); ++it) {
			m_eNodeSplit[*it] = nsExp;
			m_eOrig[*it] = eOrigExp;
		}

		pathExp.conc(path);

	} else {
		eRet = eContract;
		E.unsplit(eContract, eExpand);

		ListConstIterator<edge> it;
		for (it = path.begin(); it.valid(); ++it) {
			m_eNodeSplit[*it] = nsExp;
			m_eOrig[*it] = eOrigExp;
		}

		pathExp.concFront(path);
	}

	m_nodeSplits.del(ns->m_nsIterator);
	return eRet;
}

edge PlanRepExpansion::unsplitExpandNode(node u, edge eContract, edge eExpand) {
	NodeSplit* ns = m_eNodeSplit[eContract];
	List<edge>& path = ns->m_path;

	NodeSplit* nsExp = m_eNodeSplit[eExpand];
	edge eOrigExp = m_eOrig[eExpand];
	List<edge>& pathExp = (nsExp != nullptr) ? nsExp->m_path : m_eCopy[eOrigExp];

	if ((eExpand->target() == u && eContract->source() != u)
			|| (eExpand->source() == u && eContract->target() != u)) {
		// reverse path of eContract
		ListConstIterator<edge> it;
		for (it = path.begin(); it.valid(); ++it) {
			reverseEdge(*it);
		}
		path.reverse();
	}

	// remove u from list of copy nodes of its original
	m_vCopy[m_vOrig[u]].del(m_vIterator[u]);

	// unsplit u and enlarge edge path of eOrigExp
	edge eRet;
	if (eExpand->target() == u) {
		eRet = eExpand;
		unsplit(eExpand, eContract);

		ListConstIterator<edge> it;
		for (it = path.begin(); it.valid(); ++it) {
			m_eNodeSplit[*it] = nsExp;
			m_eOrig[*it] = eOrigExp;
		}

		pathExp.conc(path);

	} else {
		eRet = eContract;
		unsplit(eContract, eExpand);

		ListConstIterator<edge> it;
		for (it = path.begin(); it.valid(); ++it) {
			m_eNodeSplit[*it] = nsExp;
			m_eOrig[*it] = eOrigExp;
		}

		pathExp.concFront(path);
	}

	m_nodeSplits.del(ns->m_nsIterator);
	return eRet;
}

edge PlanRepExpansion::enlargeSplit(node v, edge e) {
	node vOrig = m_vOrig[v];
	edge eOrig = m_eOrig[e];

	edge eNew = split(e);
	node u = e->target();

	ListIterator<NodeSplit> itNS = m_nodeSplits.pushBack(NodeSplit());
	nodeSplit ns = &(*itNS);
	ns->m_nsIterator = itNS;

	m_vOrig[u] = vOrig;
	m_vIterator[u] = m_vCopy[vOrig].pushBack(u);
	m_splittable[u] = true;

	List<edge>& path = m_eCopy[eOrig];
	if (v == path.front()->source()) {
		ListIterator<edge> it, itNext;
		for (it = path.begin(); *it != eNew; it = itNext) {
			itNext = it.succ();

			path.moveToBack(it, ns->m_path);
			m_eOrig[*it] = nullptr;
			m_eNodeSplit[*it] = ns;
		}

	} else {
		ListIterator<edge> it, itNext;
		for (it = m_eIterator[eNew]; it.valid(); it = itNext) {
			itNext = it.succ();

			path.moveToBack(it, ns->m_path);
			m_eOrig[*it] = nullptr;
			m_eNodeSplit[*it] = ns;
		}
	}

	return eNew;
}

edge PlanRepExpansion::enlargeSplit(node v, edge e, CombinatorialEmbedding& E) {
	node vOrig = m_vOrig[v];
	edge eOrig = m_eOrig[e];

	edge eNew = E.split(e);
	node u = e->target();

	ListIterator<NodeSplit> itNS = m_nodeSplits.pushBack(NodeSplit());
	nodeSplit ns = &(*itNS);
	ns->m_nsIterator = itNS;

	m_vOrig[u] = vOrig;
	m_vIterator[u] = m_vCopy[vOrig].pushBack(u);
	m_splittable[u] = true;

	List<edge>& path = m_eCopy[eOrig];
	if (v == path.front()->source()) {
		ListIterator<edge> it, itNext;
		for (it = path.begin(); *it != eNew; it = itNext) {
			itNext = it.succ();

			path.moveToBack(it, ns->m_path);
			m_eOrig[*it] = nullptr;
			m_eNodeSplit[*it] = ns;
		}

	} else {
		ListIterator<edge> it, itNext;
		for (it = m_eIterator[eNew]; it.valid(); it = itNext) {
			itNext = it.succ();

			path.moveToBack(it, ns->m_path);
			m_eOrig[*it] = nullptr;
			m_eNodeSplit[*it] = ns;
		}
	}

	return eNew;
}

edge PlanRepExpansion::splitNodeSplit(edge e) {
	nodeSplit ns = m_eNodeSplit[e];
	node vOrig = m_vOrig[ns->source()];

	edge eNew = split(e);
	node u = e->target();

	ListIterator<NodeSplit> itNS = m_nodeSplits.pushBack(NodeSplit());
	nodeSplit nsNew = &(*itNS);
	nsNew->m_nsIterator = itNS;

	m_vOrig[u] = vOrig;
	m_vIterator[u] = m_vCopy[vOrig].pushBack(u);
	m_splittable[u] = true;

	List<edge>& path = ns->m_path;
	path.split(m_eIterator[eNew], path, nsNew->m_path);

	ListIterator<edge> it;
	for (it = nsNew->m_path.begin(); it.valid(); ++it) {
		m_eNodeSplit[*it] = nsNew;
	}

	return eNew;
}

edge PlanRepExpansion::splitNodeSplit(edge e, CombinatorialEmbedding& E) {
	nodeSplit ns = m_eNodeSplit[e];
	node vOrig = m_vOrig[ns->source()];

	edge eNew = E.split(e);
	node u = e->target();

	ListIterator<NodeSplit> itNS = m_nodeSplits.pushBack(NodeSplit());
	nodeSplit nsNew = &(*itNS);
	nsNew->m_nsIterator = itNS;

	m_vOrig[u] = vOrig;
	m_vIterator[u] = m_vCopy[vOrig].pushBack(u);
	m_splittable[u] = true;

	List<edge>& path = ns->m_path;
	path.split(m_eIterator[eNew], path, nsNew->m_path);

	ListIterator<edge> it;
	for (it = nsNew->m_path.begin(); it.valid(); ++it) {
		m_eNodeSplit[*it] = nsNew;
	}

	return eNew;
}

void PlanRepExpansion::removeSelfLoop(edge e, CombinatorialEmbedding& E) {
	node u = e->source();
	nodeSplit ns = m_eNodeSplit[e];
	edge eOrig = m_eOrig[e];

	List<edge>& path = (eOrig != nullptr) ? m_eCopy[eOrig] : ns->m_path;
	path.del(m_eIterator[e]);

	E.joinFaces(e);

	edge eIn = u->firstAdj()->theEdge();
	edge eOut = u->lastAdj()->theEdge();
	if (eIn->target() != u) {
		std::swap(eIn, eOut);
	}

	OGDF_ASSERT(ns == m_eNodeSplit[eOut]);
	OGDF_ASSERT(eOrig == m_eOrig[eOut]);

	E.unsplit(eIn, eOut);
}

void PlanRepExpansion::removeSelfLoop(edge e) {
	node u = e->source();
	nodeSplit ns = m_eNodeSplit[e];
	edge eOrig = m_eOrig[e];

	List<edge>& path = (eOrig != nullptr) ? m_eCopy[eOrig] : ns->m_path;
	path.del(m_eIterator[e]);

	delEdge(e);

	edge eIn = u->firstAdj()->theEdge();
	edge eOut = u->lastAdj()->theEdge();
	if (eIn->target() != u) {
		std::swap(eIn, eOut);
	}

	OGDF_ASSERT(ns == m_eNodeSplit[eOut]);
	OGDF_ASSERT(eOrig == m_eOrig[eOut]);

	unsplit(eIn, eOut);
}


#ifdef OGDF_DEBUG
void PlanRepExpansion::consistencyCheck() const {
	Graph::consistencyCheck();

	OGDF_ASSERT(isLoopFree(*this));

	for (edge eOrig : m_pGraph->edges) {
		const List<edge>& path = m_eCopy[eOrig];

		for (ListConstIterator<edge> it = path.begin(); it.valid(); ++it) {
			edge e = *it;

			if (it != path.begin()) {
				OGDF_ASSERT(e->source()->degree() == 4);
				OGDF_ASSERT(e->source() == (*it.pred())->target());
			}
		}
	}

	for (node vOrig : m_pGraph->nodes) {
		const List<node>& nodeList = m_vCopy[vOrig];

		if (nodeList.size() == 1) {
			OGDF_ASSERT(m_splittable[nodeList.front()] != m_splittableOrig[vOrig]);
		}

		if (nodeList.size() > 1) {
			OGDF_ASSERT(m_splittableOrig[vOrig]);

			for (node v : nodeList) {
				OGDF_ASSERT(v->degree() >= 2);
			}
		}
	}

	EdgeArray<const NodeSplit*> nso(*this, nullptr);

	for (const NodeSplit& ns : m_nodeSplits) {
		if (ns.m_path.size() != 0) {
			node v = ns.source();
			node w = ns.target();
			node vOrig = m_vOrig[v];

			OGDF_ASSERT(vOrig != nullptr);
			OGDF_ASSERT(vOrig == m_vOrig[w]);
			OGDF_ASSERT(m_splittable[v]);
			OGDF_ASSERT(m_splittable[w]);

			const List<edge>& path = ns.m_path;

			for (ListConstIterator<edge> itE = path.begin(); itE.valid(); ++itE) {
				edge e = *itE;
				nso[e] = &ns;

				if (itE != path.begin()) {
					OGDF_ASSERT(e->source()->degree() == 4);
					OGDF_ASSERT(e->source() == (*itE.pred())->target());
				}
			}
		}
	}

	for (edge e : edges) {
		OGDF_ASSERT(nso[e] == m_eNodeSplit[e]);
	}
}
#endif


List<edge>& PlanRepExpansion::setOrigs(edge e, edge& eOrig, nodeSplit& ns) {
	eOrig = m_eOrig[e];
	ns = m_eNodeSplit[e];
	return (eOrig != nullptr) ? m_eCopy[eOrig] : ns->m_path;
}

PlanRepExpansion::nodeSplit PlanRepExpansion::convertDummy(node u, node vOrig,
		PlanRepExpansion::nodeSplit ns_0) {
	OGDF_ASSERT(u->indeg() == 2);
	OGDF_ASSERT(u->outdeg() == 2);
	OGDF_ASSERT(m_vOrig[u] == nullptr);

	m_vOrig[u] = vOrig;
	m_vIterator[u] = m_vCopy[vOrig].pushBack(u);
	m_splittable[u] = true;

	edge ec[2], eOrig[2];
	PlanRepExpansion::nodeSplit nsplit[2];
	int i = 0;
	for (adjEntry adj : u->adjEntries) {
		edge e = adj->theEdge();
		if (e->source() == u) {
			ec[i] = e;
			eOrig[i] = m_eOrig[e];
			nsplit[i] = m_eNodeSplit[e];
			++i;
		}
	}
	// This assertion convinces static analyzers that we properly initialized the fields ec, eOrig, and nsplit.
	OGDF_ASSERT(i == 2);

	List<edge>& path_0 = (eOrig[0] != nullptr) ? m_eCopy[eOrig[0]] : nsplit[0]->m_path;
	if (m_vOrig[path_0.front()->source()] == vOrig) {
		path_0.split(m_eIterator[ec[0]], ns_0->m_path, path_0);
	} else {
		path_0.split(m_eIterator[ec[0]], path_0, ns_0->m_path);
	}

	ListConstIterator<edge> itE;
	for (itE = ns_0->m_path.begin(); itE.valid(); ++itE) {
		m_eNodeSplit[*itE] = ns_0;
		m_eOrig[*itE] = nullptr;
	}

	ListIterator<NodeSplit> itNS = m_nodeSplits.pushBack(NodeSplit());
	nodeSplit ns_1 = &(*itNS);
	ns_1->m_nsIterator = itNS;

	List<edge>& path_1 = (eOrig[1] != nullptr) ? m_eCopy[eOrig[1]] : nsplit[1]->m_path;
	if (m_vOrig[path_1.front()->source()] == vOrig) {
		path_1.split(m_eIterator[ec[1]], ns_1->m_path, path_1);
	} else {
		path_1.split(m_eIterator[ec[1]], path_1, ns_1->m_path);
	}

	for (itE = ns_1->m_path.begin(); itE.valid(); ++itE) {
		m_eNodeSplit[*itE] = ns_1;
		m_eOrig[*itE] = nullptr;
	}

	return ns_1;
}

edge PlanRepExpansion::separateDummy(adjEntry adj_1, adjEntry adj_2, node vStraight, bool isSrc) {
	node u = adj_1->theNode();
	OGDF_ASSERT(m_vOrig[u] == nullptr);

	node vOrig = m_vOrig[vStraight];
	node v = newNode();

	m_vOrig[v] = vOrig;
	m_vIterator[v] = m_vCopy[vOrig].pushBack(v);
	m_splittable[v] = true;

	if (adj_1->theEdge()->target() == u) {
		moveTarget(adj_1->theEdge(), v);
	} else {
		moveSource(adj_1->theEdge(), v);
	}

	if (adj_2->theEdge()->target() == u) {
		moveTarget(adj_2->theEdge(), v);
	} else {
		moveSource(adj_2->theEdge(), v);
	}

	edge eNew = (isSrc) ? newEdge(v, u) : newEdge(u, v);

	ListIterator<NodeSplit> itNS = m_nodeSplits.pushBack(NodeSplit());
	nodeSplit nsNew = &(*itNS);
	nsNew->m_nsIterator = itNS;

	edge eOrig = m_eOrig[adj_1->theEdge()];
	NodeSplit* ns = m_eNodeSplit[adj_1->theEdge()];
	List<edge>& path = (eOrig != nullptr) ? m_eCopy[eOrig] : ns->m_path;

	if (vStraight == path.front()->source()) {
		ListIterator<edge> it, itNext;
		for (it = path.begin(); (*it)->source() != v; it = itNext) {
			itNext = it.succ();
			path.moveToBack(it, nsNew->m_path);
			m_eOrig[*it] = nullptr;
			m_eNodeSplit[*it] = nsNew;
		}

	} else {
		ListReverseIterator<edge> it, itPrev;
		for (it = path.rbegin(); (*it)->target() != v; it = itPrev) {
			itPrev = it.succ();
			path.moveToFront(it, nsNew->m_path);
			m_eOrig[*it] = nullptr;
			m_eNodeSplit[*it] = nsNew;
		}
	}

	return eNew;

#if 0
	edge       eOrig = m_eOrig[adjStraight->theEdge()];
	NodeSplit *ns    = m_eNodeSplit[adjStraight->theEdge()];

	Array<adjEntry> adjA(2), adjB(2);
	int i = 0, j = 0;

	for(adjEntry adj : u->adjEntries) {
		edge e = adj->theEdge();
		if(m_eOrig[e] == eOrig && m_eNodeSplit[e] == ns)
			adjA[i++] = adj;
		else
			adjB[j++] = adj;
	}

	OGDF_ASSERT(i == 2);
	OGDF_ASSERT(j == 2);

	// resolve split on adjB
	edge eB = adjB[0]->theEdge();
	node vB = adjB[1]->twinNode();

	edge eOrigB;
	NodeSplit *nsB;
	List<edge> &pathB = (m_eOrig[eB] != 0) ? m_eCopy[m_eOrig[eB]] : m_eNodeSplit[eB]->m_path;

	if(eB->target() == u)
		moveTarget(eB,vB);
	else
		moveSource(eB,vB);

	edge eB2 = adjB[1]->theEdge();
	pathB.del(m_eIterator[eB2]);
	delEdge(eB2);

	// split path at u
	node vOrig = m_vOrig[vStraight];

	m_vOrig     [u] = vOrig;
	m_vIterator [u] = m_vCopy[vOrig].pushBack(u);
	m_splittable[u] = true;

	ListIterator<NodeSplit> itNS = m_nodeSplits.pushBack(NodeSplit());
	nodeSplit nsNew = &(*itNS);
	nsNew->m_nsIterator = itNS;

	List<edge> &pathA = (eOrig != 0) ? m_eCopy[eOrig] : ns->m_path;
	if(vStraight == pathA.front()->source()) {
		ListIterator<edge> it, itNext;
		for(it = pathA.begin(); (*it)->source() != u; it = itNext) {
			itNext = it.succ();
			pathA.moveToBack(it, nsNew->m_path);
			m_eOrig     [*it] = 0;
			m_eNodeSplit[*it] = nsNew;
		}

	} else {
		ListReverseIterator<edge> it, itPrev;
		for(it = pathA.rbegin(); (*it)->target() != u; it = itPrev) {
			itPrev = it.succ();
			pathA.moveToFront(it, nsNew->m_path);
			m_eOrig     [*it] = 0;
			m_eNodeSplit[*it] = nsNew;
		}
	}
#endif
}

int PlanRepExpansion::numberOfSplittedNodes() const {
	int num = 0;

	for (node vOrig : m_pGraph->nodes) {
		if (m_vCopy[vOrig].size() >= 2) {
			++num;
		}
	}

	return num;
}

bool PlanRepExpansion::isPseudoCrossing(node v) const {
	if (m_vOrig[v] != nullptr) {
		return false;
	}

	adjEntry adj_1 = v->firstAdj();
	adjEntry adj_2 = adj_1->succ();
	adjEntry adj_3 = adj_2->succ();

	edge eOrig = m_eOrig[adj_2->theEdge()];
	nodeSplit ns = m_eNodeSplit[adj_2->theEdge()];

	if (m_eNodeSplit[adj_1->theEdge()] == ns && m_eOrig[adj_1->theEdge()] == eOrig) {
		return true;
	}

	if (m_eNodeSplit[adj_3->theEdge()] == ns && m_eOrig[adj_3->theEdge()] == eOrig) {
		return true;
	}

	return false;
}

void PlanRepExpansion::resolvePseudoCrossing(node v) {
	OGDF_ASSERT(isPseudoCrossing(v));

	edge eIn[2];
	int i = 0;
	for (adjEntry adj : v->adjEntries) {
		edge e = adj->theEdge();
		if (e->target() == v) {
			eIn[i++] = e;
		}
	}
	OGDF_ASSERT(i == 2);

	for (i = 0; i < 2; ++i) {
		edge e = eIn[i];

		ListIterator<edge> it = m_eIterator[e];
		List<edge>& path = (m_eOrig[e] != nullptr) ? m_eCopy[m_eOrig[e]] : m_eNodeSplit[e]->m_path;

		edge eNext = *(it.succ());
		moveSource(eNext, e->source());
		path.del(it);
		delEdge(e);
	}
}

}
