/** \file
 * \brief Implementation of Hypergraph class.
 *
 * \author Ondrej Moris
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
#include <ogdf/basic/GraphList.h>
#include <ogdf/basic/HashArray.h>
#include <ogdf/basic/List.h>
#include <ogdf/basic/basic.h>
#include <ogdf/hypergraph/Hypergraph.h>
#include <ogdf/hypergraph/HypergraphObserver.h>

#include <cstring>
#include <fstream>
#include <functional>
#include <string>

namespace ogdf {

Hypergraph::Hypergraph()
	: m_regHypernodeArrays(this, &m_hypernodeIdCount)
	, m_regHyperedgeArrays(this, &m_hyperedgeIdCount) {
	m_nHypernodes = m_nHyperedges = 0;
	m_hypernodeIdCount = m_hyperedgeIdCount = 0;
	initArrays();
}

Hypergraph::~Hypergraph() {
	clearObservers();

	for (hypernode v = m_hypernodes.head(); v; v = v->succ()) {
		v->m_adjHyperedges.~GraphList<AdjHypergraphElement>();
	}

	for (hyperedge e = m_hyperedges.head(); e; e = e->succ()) {
		e->m_adjHypernodes.~GraphList<AdjHypergraphElement>();
	}
}

hypernode Hypergraph::newHypernode(int pIndex) {
	++m_nHypernodes;

	hypernode v = new HypernodeElement(pIndex);
	if (m_hypernodeIdCount <= pIndex) {
		m_hypernodeIdCount = pIndex + 1;
	}

	m_hypernodes.pushBack(v);
	m_regHypernodeArrays.keyAdded(v);

	v->m_hypergraph = this;

	for (HypergraphObserver* obs : getObservers()) {
		obs->hypernodeAdded(v);
	}

	return v;
}

hypernode Hypergraph::newHypernode() { return newHypernode(m_hypernodeIdCount); }

hypernode Hypergraph::newHypernode(HypernodeElement::Type pType) {
	hypernode v = newHypernode();
	v->m_type = pType;
	return v;
}

hypernode Hypergraph::newHypernode(int pIndex, HypernodeElement::Type pType) {
	hypernode v = newHypernode(pIndex);
	v->m_type = pType;
	return v;
}

hyperedge Hypergraph::newHyperedge(int pIndex, List<hypernode>& pHypernodes) {
	OGDF_ASSERT(pHypernodes.size() > 1);
	for (ListIterator<hypernode> it = pHypernodes.begin(); it.valid(); ++it) {
		OGDF_ASSERT((*it)->hypergraph() == this);
	}

	++m_nHyperedges;

	hyperedge e = new HyperedgeElement(pIndex);
	e->m_hypergraph = this;
	m_hyperedges.pushBack(e);
	m_regHyperedgeArrays.keyAdded(e);

	if (m_hyperedgeIdCount <= pIndex) {
		m_hyperedgeIdCount = pIndex + 1;
	}

	for (HypergraphObserver* obs : getObservers()) {
		obs->hyperedgeAdded(e);
	}

	for (ListIterator<hypernode> it = pHypernodes.begin(); it.valid(); ++it) {
		hypernode v = *it;

		adjHypergraphEntry adjHypergraphEntryE = new AdjHypergraphElement(v);
		adjHypergraphEntry adjHypergraphEntryV = new AdjHypergraphElement(e);

		adjHypergraphEntryE->m_twin = adjHypergraphEntryV;
		adjHypergraphEntryV->m_twin = adjHypergraphEntryE;

		e->m_adjHypernodes.pushBack(adjHypergraphEntryE);
		v->m_adjHyperedges.pushBack(adjHypergraphEntryV);

		v->m_degree++;
		e->m_cardinality++;
	}

	return e;
}

hyperedge Hypergraph::newHyperedge(List<hypernode>& pHypernodes) {
	return newHyperedge(m_hyperedgeIdCount, pHypernodes);
}

void Hypergraph::delHypernode(hypernode v) {
	OGDF_ASSERT(v != nullptr);

	safeForEach(v->m_adjHyperedges, [&](adjHypergraphEntry adj) {
		hyperedge e = static_cast<hyperedge>(adj->element());

		e->m_adjHypernodes.del(adj->twin());
		v->m_adjHyperedges.del(adj);

		e->m_cardinality--;
		if (e->m_cardinality < 2) {
			delHyperedge(e);
		}

		v->m_degree--;
	});
	OGDF_ASSERT(v->m_degree == 0);

	m_regHypernodeArrays.keyRemoved(v);
	for (HypergraphObserver* obs : getObservers()) {
		obs->hypernodeDeleted(v);
	}

	--m_nHypernodes;
	m_hypernodes.del(v);
}

void Hypergraph::delHyperedge(hyperedge e) {
	OGDF_ASSERT(e != nullptr);

	m_regHyperedgeArrays.keyRemoved(e);
	for (HypergraphObserver* obs : getObservers()) {
		obs->hyperedgeDeleted(e);
	}

	safeForEach(e->m_adjHypernodes, [&](adjHypergraphEntry adj) {
		hypernode n = static_cast<hypernode>(adj->element());
		n->m_degree--;
		n->m_adjHyperedges.del(adj->twin());
		e->m_adjHypernodes.del(adj);
		e->m_cardinality--;
	});
	OGDF_ASSERT(e->m_cardinality == 0);

	--m_nHyperedges;
	m_hyperedges.del(e);
}

void Hypergraph::clear() {
	for (HypergraphObserver* obs : getObservers()) {
		obs->cleared();
	}

	for (hyperedge e = m_hyperedges.head(); e; e = e->succ()) {
		e->m_adjHypernodes.~GraphList<AdjHypergraphElement>();
	}

	for (hypernode v = m_hypernodes.head(); v; v = v->succ()) {
		v->m_adjHyperedges.~GraphList<AdjHypergraphElement>();
	}

	m_hypernodes.clear();
	m_hyperedges.clear();

	m_nHypernodes = m_nHyperedges = 0;
	m_hypernodeIdCount = m_hyperedgeIdCount = 0;

	m_regHypernodeArrays.keysCleared();
	m_regHyperedgeArrays.keysCleared();

	initArrays();
}

hypernode Hypergraph::randomHypernode() const {
	if (m_nHypernodes == 0) {
		return nullptr;
	}

	hypernode v = firstHypernode();
	for (int i = ogdf::randomNumber(0, m_nHypernodes - 1); i; i--, v = v->succ()) {
		;
	}

	return v;
}

hyperedge Hypergraph::randomHyperedge() const {
	if (m_nHyperedges == 0) {
		return nullptr;
	}

	hyperedge e = firstHyperedge();
	for (int i = ogdf::randomNumber(0, m_nHyperedges - 1); i; i--, e = e->succ()) {
		;
	}

	return e;
}

void Hypergraph::initArrays() {
	m_regHypernodeArrays.resizeArrays(0);
	m_regHypernodeArrays.resizeArrays();

	m_regHyperedgeArrays.resizeArrays(0);
	m_regHyperedgeArrays.resizeArrays();
}

bool Hypergraph::consistency() const {
	if (m_nHypernodes != hypernodes().size()) {
		return false;
	}

	if (m_nHyperedges != hyperedges().size()) {
		return false;
	}

	if (m_nHypernodes > m_hypernodeIdCount) {
		return false;
	}

	if (m_nHyperedges > m_hyperedgeIdCount) {
		return false;
	}

	for (hypernode v = m_hypernodes.head(); v; v = v->succ()) {
		if (v->m_hypergraph != this) {
			return false;
		}

		if (v->m_adjHyperedges.size() != v->m_degree) {
			return false;
		}

		for (adjHypergraphEntry adj = v->m_adjHyperedges.head(); adj; adj = adj->succ()) {
			if (reinterpret_cast<hypernode>(adj->twin()->element()) != v) {
				return false;
			}
		}
	}

	for (hyperedge e = m_hyperedges.head(); e; e = e->succ()) {
		if (e->m_hypergraph != this) {
			return false;
		}

		if (e->m_adjHypernodes.size() != e->m_cardinality) {
			return false;
		}

		for (adjHypergraphEntry adj = e->m_adjHypernodes.head(); adj; adj = adj->succ()) {
			if (reinterpret_cast<hyperedge>(adj->twin()->element()) != e) {
				return false;
			}
		}

		if (e->m_cardinality < 2) {
			return false;
		}
	}

	return true;
}

std::ostream& operator<<(std::ostream& os, ogdf::adjHypergraphEntry v) {
	if (v) {
		os << "[" << v->index() << "]";
	} else {
		os << "nil";
	}
	return os;
}

std::ostream& operator<<(std::ostream& os, ogdf::hypernode v) {
	if (v) {
		os << v->index();
	} else {
		os << "nil";
	}
	return os;
}

std::ostream& operator<<(std::ostream& os, ogdf::hyperedge e) {
	if (e) {
		os << e->index() << " " << e->cardinality() << " ";
		for (adjHypergraphEntry adj = e->m_adjHypernodes.head(); adj; adj = adj->succ()) {
			os << reinterpret_cast<hypernode>(adj->element())->index() << " ";
		}
	} else {
		os << "nil";
	}

	return os;
}

std::ostream& operator<<(std::ostream& os, ogdf::Hypergraph& H) {
	os << H.m_nHypernodes << " " << H.m_hypernodeIdCount << std::endl;

	hypernode v;
	forall_hypernodes(v, H) {
		os << v << std::endl;
	}

	os << H.m_nHyperedges << " " << H.m_hyperedgeIdCount << std::endl;

	hyperedge e;
	forall_hyperedges(e, H) {
		os << e << std::endl;
	}

	return os;
}

std::istream& operator>>(std::istream& is, ogdf::Hypergraph& H) {
	int nHypernodes, nHyperedges, hypernodeIdCount, hyperedgeIdCount;

	is >> nHypernodes;
	is >> hypernodeIdCount;
	Array<hypernode> hypernodeIndex(hypernodeIdCount);
	for (int i = 0; i < nHypernodes; i++) {
		int index;
		is >> index;
		OGDF_ASSERT(index < hypernodeIdCount);
		hypernodeIndex[index] = H.newHypernode(index);
	}

	is >> nHyperedges;
	is >> hyperedgeIdCount;
	for (int i = 0; i < nHyperedges; i++) {
		int cardinality, index;
		List<hypernode> hypernodes;
		is >> index;
		is >> cardinality;
		OGDF_ASSERT(index < hyperedgeIdCount);
		for (int j = 0; j < cardinality; j++) {
			int hIndex;
			is >> hIndex;
			hypernodes.pushBack(hypernodeIndex[hIndex]);
		}
		H.newHyperedge(index, hypernodes);
	}

	return is;
}

HypergraphRegistry<HypernodeElement>::iterator begin(
		const HypergraphRegistry<HypernodeElement>& self) {
	return self.graphOf()->hypernodes().begin();
}

HypergraphRegistry<HypernodeElement>::iterator end(const HypergraphRegistry<HypernodeElement>& self) {
	return self.graphOf()->hypernodes().end();
}

HypergraphRegistry<HyperedgeElement>::iterator begin(
		const HypergraphRegistry<HyperedgeElement>& self) {
	return self.graphOf()->hyperedges().begin();
}

HypergraphRegistry<HyperedgeElement>::iterator end(const HypergraphRegistry<HyperedgeElement>& self) {
	return self.graphOf()->hyperedges().end();
}

void Hypergraph::readBenchHypergraph(std::istream& is) {
	// The map from identifiers to hypernodes.
	HashArray<string, hypernode> map(nullptr);

	while (!is.eof()) {
		char buffer[2048];

		// Read the line till the end.
		is.getline(buffer, 2048 - 1);

		// Ignore comments, special and empty lines.
		if (!strlen(buffer) || buffer[0] == ' ' || buffer[0] == '#') {
			continue;
		}

		if (!strncmp("INPUT(", buffer, 6)) {
			// INPUT
			string s(buffer + 6, nextEntry(buffer, 6, ")\0"));
			hypernode n = newHypernode(HypernodeElement::Type::INPUT);
			map[s] = n;

		} else if (!strncmp("OUTPUT(", buffer, 7)) {
			// OUTPUT
			string s(buffer + 7, nextEntry(buffer, 7, ")\0"));
			hypernode n = newHypernode(HypernodeElement::Type::OUTPUT);
			map[s] = n;

		} else {
			// GATES / BUFFERS / FLOPS

			// The list of all hyperedge hypernodes.
			List<hypernode> hypernodes;

			int pos = nextEntry(buffer, 0, " ");
			string str(buffer, pos);
			hypernode in = map[str];

			// Determining the type of this gate
			pos += nextEntry(buffer, pos, "=");
			pos += nextEntry(buffer, pos, " ") + 1;
			string typeStr = string(buffer + pos, nextEntry(buffer, pos, "(\0"));
			HypernodeElement::Type type = gateType(typeStr);

			if (!in) {
				in = newHypernode(type);
				map[str] = in;
			} else {
				in->m_type = type;
			}

			hypernodes.pushBack(in);

			// Determining the hypernodes connected by this gate.
			pos += nextEntry(buffer, pos, "(\0") + 1;
			while (true) {
				if (buffer[pos] == ')' || buffer[pos] == '\0' || buffer[pos] == '\r') {
					break;
				}

				str = string(buffer + pos, nextEntry(buffer, pos, ",)\0"));
				pos += nextEntry(buffer, pos, ",)\0");

				hypernode out = map[str];
				if (!out) {
					out = newHypernode();
					map[str] = out;
				}
				hypernodes.pushBack(out);

				if (buffer[pos] == ',') {
					pos++;
				}
				if (buffer[pos] == ' ') {
					pos++;
				}
			}
			newHyperedge(hypernodes);
		}
	}
}

void Hypergraph::readBenchHypergraph(const char* filename) {
	std::ifstream is(filename);

	if (!is.good()) {
		return;
	}

	return readBenchHypergraph(is);
}

int Hypergraph::nextEntry(char* buffer, int from, string stop) {
	int pos = from;

	while (buffer[pos] != '\r' && buffer[pos] != '\0') {
		for (int i = 0; stop[i] != '\0'; i++) {
			if (buffer[pos] == stop[i]) {
				return pos - from;
			}
		}
		pos++;
	}

	return pos - from;
}

HypernodeElement::Type Hypergraph::gateType(string gate) {
	if (!gate.compare("or")) {
		return HypernodeElement::Type::OR;
	} else if (!gate.compare("and") || !gate.compare("AND")) {
		return HypernodeElement::Type::AND;
	} else if (!gate.compare("nor") || !gate.compare("NOR")) {
		return HypernodeElement::Type::NOR;
	} else if (!gate.compare("not") || !gate.compare("NOT")) {
		return HypernodeElement::Type::NOT;
	} else if (!gate.compare("xor") || !gate.compare("XOR")) {
		return HypernodeElement::Type::XOR;
	} else if (!gate.compare("buf") || !gate.compare("BUF")) {
		return HypernodeElement::Type::BUF;
	} else if (!gate.compare("nand") || !gate.compare("NAND")) {
		return HypernodeElement::Type::NAND;
	} else if (!gate.compare("dff") || !gate.compare("DFF")) {
		return HypernodeElement::Type::DFF;
	}

	return HypernodeElement::Type::normal;
}

}
