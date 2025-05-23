/** \file
 * \brief Implementation of the class EmbedPQTree.
 *
 * Implements a PQTree with added features for the planar
 * embedding algorithm. Used by BoothLueker.
 *
 * \author Sebastian Leipert
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

#include <ogdf/basic/ArrayBuffer.h>
#include <ogdf/basic/Graph.h>
#include <ogdf/basic/List.h>
#include <ogdf/basic/PQTree.h>
#include <ogdf/basic/SList.h>
#include <ogdf/basic/basic.h>
#include <ogdf/basic/pqtree/PQBasicKey.h>
#include <ogdf/basic/pqtree/PQInternalNode.h>
#include <ogdf/basic/pqtree/PQLeaf.h>
#include <ogdf/basic/pqtree/PQLeafKey.h>
#include <ogdf/basic/pqtree/PQNode.h>
#include <ogdf/basic/pqtree/PQNodeKey.h>
#include <ogdf/basic/pqtree/PQNodeRoot.h>
#include <ogdf/planarity/booth_lueker/EmbedIndicator.h>
#include <ogdf/planarity/booth_lueker/EmbedPQTree.h>
#include <ogdf/planarity/booth_lueker/IndInfo.h>
#include <ogdf/planarity/booth_lueker/PlanarLeafKey.h>

namespace ogdf {
namespace booth_lueker {

// Replaces the pertinent subtree by a P-node with leaves as children
// corresponding to the incoming edges of the node v. These edges
// are to be specified by their keys stored in leafKeys.
// The function returns the frontier of the pertinent subtree and
// the direction indicators found within the pertinent leaves.
// The direction indicators are returned in two list:
// opposed: containing the keys of indicators pointing into reverse
// frontier scanning direction (thus their corsponding list has to be
// reversed.
// nonOpposed: containing the keys of indicators pointing into
// frontier scanning direction (thus their corsponding list do not need
// reversed in the first place)

void EmbedPQTree::ReplaceRoot(SListPure<PlanarLeafKey<IndInfo*>*>& leafKeys,
		SListPure<edge>& frontier, SListPure<node>& opposed, SListPure<node>& nonOpposed, node v) {
	SListPure<PQBasicKey<edge, IndInfo*, bool>*> nodeFrontier;

	if (leafKeys.empty() && m_pertinentRoot == m_root) {
		front(m_pertinentRoot, nodeFrontier);
		m_pertinentRoot = nullptr; // check for this emptyAllPertinentNodes

	} else {
		if (m_pertinentRoot->status() == PQNodeRoot::PQNodeStatus::Full) {
			ReplaceFullRoot(leafKeys, nodeFrontier, v);
		} else {
			ReplacePartialRoot(leafKeys, nodeFrontier, v);
		}
	}

	// Check the frontier and get the direction indicators.
	while (!nodeFrontier.empty()) {
		PQBasicKey<edge, IndInfo*, bool>* entry = nodeFrontier.popFrontRet();
		if (entry->userStructKey()) { // is a regular leaf
			frontier.pushBack(entry->userStructKey());
		}

		else if (entry->userStructInfo()) {
			if (entry->userStructInfo()->changeDir) {
				opposed.pushBack(entry->userStructInfo()->v);
			} else {
				nonOpposed.pushBack(entry->userStructInfo()->v);
			}
		}
	}
}

// The function [[emptyAllPertinentNodes]] has to be called after a reduction
// has been processed. This overloaded function first destroys all full nodes
// by marking them as ToBeDeleted and then calling the base class function
// [[emptyAllPertinentNodes]].
void EmbedPQTree::emptyAllPertinentNodes() {
	for (PQNode<edge, IndInfo*, bool>* nodePtr : *m_pertinentNodes) {
		if (nodePtr->status() == PQNodeRoot::PQNodeStatus::Full) {
			destroyNode(nodePtr);
		}
	}
	if (m_pertinentRoot) { // Node was kept in the tree. Do not free it.
		m_pertinentRoot->status(PQNodeRoot::PQNodeStatus::Full);
	}

	PQTree<edge, IndInfo*, bool>::emptyAllPertinentNodes();
}

void EmbedPQTree::clientDefinedEmptyNode(PQNode<edge, IndInfo*, bool>* nodePtr) {
	if (nodePtr->status() == PQNodeRoot::PQNodeStatus::Indicator) {
		delete nodePtr;
	} else {
		PQTree<edge, IndInfo*, bool>::clientDefinedEmptyNode(nodePtr);
	}
}

// Initializes a PQTree by a set of leaves that will korrespond to
// the set of Keys stored in leafKeys.
int EmbedPQTree::Initialize(SListPure<PlanarLeafKey<IndInfo*>*>& leafKeys) {
	SListPure<PQLeafKey<edge, IndInfo*, bool>*> castLeafKeys;
	for (PlanarLeafKey<IndInfo*>* key : leafKeys) {
		castLeafKeys.pushBack(static_cast<PQLeafKey<edge, IndInfo*, bool>*>(key));
	}

	return PQTree<edge, IndInfo*, bool>::Initialize(castLeafKeys);
}

// Reduction reduced a set of leaves determined by their keys stored
// in leafKeys. Integer redNumber is for debugging only.
bool EmbedPQTree::Reduction(SListPure<PlanarLeafKey<IndInfo*>*>& leafKeys) {
	SListPure<PQLeafKey<edge, IndInfo*, bool>*> castLeafKeys;
	for (PlanarLeafKey<IndInfo*>* key : leafKeys) {
		castLeafKeys.pushBack(static_cast<PQLeafKey<edge, IndInfo*, bool>*>(key));
	}

	return PQTree<edge, IndInfo*, bool>::Reduction(castLeafKeys);
}

// Function ReplaceFullRoot either replaces the full root
// or one full child of a partial root of a pertinent subtree
// by a single P-node  with leaves corresponding the keys stored in leafKeys.
// Furthermore it scans the frontier of the pertinent subtree, and returns it
// in frontier.
// If called by ReplacePartialRoot, the function ReplaceFullRoot handles
// the introduction of the direction  indicator. (This must be indicated
// by addIndicator.
// Node v determines the node related to the pertinent leaves. It is needed
// to assign the dirrection indicator to this sequence.

void EmbedPQTree::ReplaceFullRoot(SListPure<PlanarLeafKey<IndInfo*>*>& leafKeys,
		SListPure<PQBasicKey<edge, IndInfo*, bool>*>& frontier, node v, bool addIndicator,
		PQNode<edge, IndInfo*, bool>* opposite) {
	EmbedIndicator* newInd = nullptr;

	front(m_pertinentRoot, frontier);
	if (addIndicator) {
		IndInfo* newInfo = new IndInfo(v);
		PQNodeKey<edge, IndInfo*, bool>* nodeInfoPtr = new PQNodeKey<edge, IndInfo*, bool>(newInfo);
		newInd = new EmbedIndicator(m_identificationNumber++, nodeInfoPtr);
		newInd->setNodeInfo(nodeInfoPtr);
		nodeInfoPtr->setNodePointer(newInd);
	}

	if (!leafKeys.empty() && leafKeys.front() == leafKeys.back()) {
		//ReplaceFullRoot: replace pertinent root by a single leaf
		if (addIndicator) {
			opposite = m_pertinentRoot->getNextSib(opposite);
			if (!opposite) // m_pertinentRoot is endmost child
			{
				addNodeToNewParent(m_pertinentRoot->parent(), newInd, m_pertinentRoot, opposite);
			} else {
				addNodeToNewParent(nullptr, newInd, m_pertinentRoot, opposite);
			}

			// Setting the sibling pointers into opposite direction of
			// scanning the front allows to track swaps of the indicator
			newInd->changeSiblings(m_pertinentRoot, nullptr);
			newInd->changeSiblings(opposite, nullptr);
			newInd->putSibling(m_pertinentRoot, PQNodeRoot::SibDirection::Left);
			newInd->putSibling(opposite, PQNodeRoot::SibDirection::Right);
		}
		PQLeaf<edge, IndInfo*, bool>* leafPtr = new PQLeaf<edge, IndInfo*, bool>(
				m_identificationNumber++, PQNodeRoot::PQNodeStatus::Empty,
				(PQLeafKey<edge, IndInfo*, bool>*)leafKeys.front());
		exchangeNodes(m_pertinentRoot, (PQNode<edge, IndInfo*, bool>*)leafPtr);
		if (m_pertinentRoot == m_root) {
			m_root = (PQNode<edge, IndInfo*, bool>*)leafPtr;
		}
		m_pertinentRoot = nullptr; // check for this emptyAllPertinentNodes
	}

	else if (!leafKeys.empty()) // at least two leaves
	{
		//replace pertinent root by a $P$-node
		if (addIndicator) {
			opposite = m_pertinentRoot->getNextSib(opposite);
			if (!opposite) // m_pertinentRoot is endmost child
			{
				addNodeToNewParent(m_pertinentRoot->parent(), newInd, m_pertinentRoot, opposite);
			} else {
				addNodeToNewParent(nullptr, newInd, m_pertinentRoot, opposite);
			}

			// Setting the sibling pointers into opposite direction of
			// scanning the front allows to track swaps of the indicator
			newInd->changeSiblings(m_pertinentRoot, nullptr);
			newInd->changeSiblings(opposite, nullptr);
			newInd->putSibling(m_pertinentRoot, PQNodeRoot::SibDirection::Left);
			newInd->putSibling(opposite, PQNodeRoot::SibDirection::Right);
		}

		PQInternalNode<edge, IndInfo*, bool>* nodePtr = nullptr; // dummy
		if ((m_pertinentRoot->type() == PQNodeRoot::PQNodeType::PNode)
				|| (m_pertinentRoot->type() == PQNodeRoot::PQNodeType::QNode)) {
			nodePtr = (PQInternalNode<edge, IndInfo*, bool>*)m_pertinentRoot;
			nodePtr->type(PQNodeRoot::PQNodeType::PNode);
			nodePtr->childCount(0);
			while (!fullChildren(m_pertinentRoot)->empty()) {
				PQNode<edge, IndInfo*, bool>* currentNode =
						fullChildren(m_pertinentRoot)->popFrontRet();
				removeChildFromSiblings(currentNode);
			}
		} else if (m_pertinentRoot->type() == PQNodeRoot::PQNodeType::Leaf) {
			nodePtr = new PQInternalNode<edge, IndInfo*, bool>(m_identificationNumber++,
					PQNodeRoot::PQNodeType::PNode, PQNodeRoot::PQNodeStatus::Empty);
			exchangeNodes(m_pertinentRoot, nodePtr);
			m_pertinentRoot = nullptr; // check for this emptyAllPertinentNodes
		}

		SListPure<PQLeafKey<edge, IndInfo*, bool>*> castLeafKeys;
		for (PlanarLeafKey<IndInfo*>* key : leafKeys) {
			castLeafKeys.pushBack(static_cast<PQLeafKey<edge, IndInfo*, bool>*>(key));
		}
		addNewLeavesToTree(nodePtr, castLeafKeys);
	}
}

// Function ReplacePartialRoot replaces all full nodes by a single P-node
// with leaves corresponding the keys stored in leafKeys.
// Furthermore it scans the frontier of the pertinent subtree, and returns it
// in frontier.
// node v determines the node related to the pertinent leaves. It is needed
// to assign the dirrection indicator to this sequence.

void EmbedPQTree::ReplacePartialRoot(SListPure<PlanarLeafKey<IndInfo*>*>& leafKeys,
		SListPure<PQBasicKey<edge, IndInfo*, bool>*>& frontier, node v) {
	m_pertinentRoot->childCount(
			m_pertinentRoot->childCount() + 1 - fullChildren(m_pertinentRoot)->size());

	PQNode<edge, IndInfo*, bool>* predNode = nullptr; // dummy
	PQNode<edge, IndInfo*, bool>* beginSequence = nullptr; // marks begin consecuitve seqeunce
	PQNode<edge, IndInfo*, bool>* endSequence = nullptr; // marks end consecutive sequence
	// initially, marks direct sibling indicator next to beginSequence not
	// contained in consectuive sequence
	PQNode<edge, IndInfo*, bool>* beginInd = nullptr;

	// Get beginning and end of sequence.
	while (fullChildren(m_pertinentRoot)->size()) {
		PQNode<edge, IndInfo*, bool>* currentNode = fullChildren(m_pertinentRoot)->popFrontRet();
		if (!clientSibLeft(currentNode)
				|| clientSibLeft(currentNode)->status() == PQNodeRoot::PQNodeStatus::Empty) {
			if (!beginSequence) {
				beginSequence = currentNode;
				predNode = clientSibLeft(currentNode);
				beginInd = PQTree<edge, IndInfo*, bool>::clientSibLeft(currentNode);
			} else {
				endSequence = currentNode;
			}
		} else if (!clientSibRight(currentNode)
				|| clientSibRight(currentNode)->status() == PQNodeRoot::PQNodeStatus::Empty) {
			if (!beginSequence) {
				beginSequence = currentNode;
				predNode = clientSibRight(currentNode);
				beginInd = PQTree<edge, IndInfo*, bool>::clientSibRight(currentNode);
			} else {
				endSequence = currentNode;
			}
		}
	}

	SListPure<PQBasicKey<edge, IndInfo*, bool>*> partialFrontier;


	// Now scan the sequence of full nodes. Remove all of them but the last.
	// Call ReplaceFullRoot on the last one.
	// For every full node get its frontier. Scan intermediate indicators.

	OGDF_ASSERT(beginSequence != nullptr);
	OGDF_ASSERT(endSequence != nullptr);
	PQNode<edge, IndInfo*, bool>* currentNode = beginSequence;
	while (currentNode != endSequence) {
		PQNode<edge, IndInfo*, bool>* nextNode = clientNextSib(currentNode, predNode);
		front(currentNode, partialFrontier);
		frontier.conc(partialFrontier);

		PQNode<edge, IndInfo*, bool>* currentInd =
				PQTree<edge, IndInfo*, bool>::clientNextSib(currentNode, beginInd);

		// Scan for intermediate direction indicators.
		while (currentInd != nextNode) {
			PQNode<edge, IndInfo*, bool>* nextInd =
					PQTree<edge, IndInfo*, bool>::clientNextSib(currentInd, currentNode);
			if (currentNode == currentInd->getSib(PQNodeRoot::SibDirection::Right)) { //Direction changed
				currentInd->getNodeInfo()->userStructInfo()->changeDir = true;
			}
			frontier.pushBack((PQBasicKey<edge, IndInfo*, bool>*)currentInd->getNodeInfo());
			removeChildFromSiblings(currentInd);
			m_pertinentNodes->pushBack(currentInd);
			currentInd = nextInd;
		}

		removeChildFromSiblings(currentNode);
		currentNode = nextNode;
		OGDF_ASSERT(currentNode != nullptr);
	}

	currentNode->parent(m_pertinentRoot);
	m_pertinentRoot = currentNode;
	ReplaceFullRoot(leafKeys, partialFrontier, v, true, beginInd);
	frontier.conc(partialFrontier);
}

// Overloads virtual function of base class PQTree
// Allows ignoring the virtual direction indicators during
// the template matching algorithm.
PQNode<edge, IndInfo*, bool>* EmbedPQTree::clientSibLeft(PQNode<edge, IndInfo*, bool>* nodePtr) const {
	PQNode<edge, IndInfo*, bool>* predNode = nodePtr;
	nodePtr = PQTree<edge, IndInfo*, bool>::clientSibLeft(predNode);
	while (nodePtr && nodePtr->status() == PQNodeRoot::PQNodeStatus::Indicator) {
		PQNode<edge, IndInfo*, bool>* holdSib = predNode;
		predNode = nodePtr;
		nodePtr = predNode->getNextSib(holdSib);
	}

	return nodePtr;
}

// Overloads virtual function of base class PQTree
// Allows ignoring the virtual direction indicators during
// the template matching algorithm.
PQNode<edge, IndInfo*, bool>* EmbedPQTree::clientSibRight(
		PQNode<edge, IndInfo*, bool>* nodePtr) const {
	PQNode<edge, IndInfo*, bool>* predNode = nodePtr;
	nodePtr = PQTree<edge, IndInfo*, bool>::clientSibRight(predNode);
	while (nodePtr && nodePtr->status() == PQNodeRoot::PQNodeStatus::Indicator) {
		PQNode<edge, IndInfo*, bool>* holdSib = predNode;
		predNode = nodePtr;
		nodePtr = predNode->getNextSib(holdSib);
	}

	return nodePtr;
}

// Overloads virtual function of base class PQTree
// Allows ignoring the virtual direction indicators during
// the template matching algorithm.
PQNode<edge, IndInfo*, bool>* EmbedPQTree::clientLeftEndmost(
		PQNode<edge, IndInfo*, bool>* nodePtr) const {
	PQNode<edge, IndInfo*, bool>* left = PQTree<edge, IndInfo*, bool>::clientLeftEndmost(nodePtr);

	if (!left || left->status() != PQNodeRoot::PQNodeStatus::Indicator) {
		return left;
	} else {
		return clientNextSib(left, nullptr);
	}
}

// Overloads virtual function of base class PQTree
// Allows ignoring the virtual direction indicators during
// the template matching algorithm.
PQNode<edge, IndInfo*, bool>* EmbedPQTree::clientRightEndmost(
		PQNode<edge, IndInfo*, bool>* nodePtr) const {
	PQNode<edge, IndInfo*, bool>* right = PQTree<edge, IndInfo*, bool>::clientRightEndmost(nodePtr);

	if (!right || right->status() != PQNodeRoot::PQNodeStatus::Indicator) {
		return right;
	} else {
		return clientNextSib(right, nullptr);
	}
}

// Overloads virtual function of base class PQTree
// Allows ignoring the virtual direction indicators during
// the template matching algorithm.
PQNode<edge, IndInfo*, bool>* EmbedPQTree::clientNextSib(PQNode<edge, IndInfo*, bool>* nodePtr,
		PQNode<edge, IndInfo*, bool>* other) const {
	PQNode<edge, IndInfo*, bool>* left = clientSibLeft(nodePtr);
	if (left != other) {
		return left;
	}

	PQNode<edge, IndInfo*, bool>* right = clientSibRight(nodePtr);
	if (right != other) {
		return right;
	}

	return nullptr;
}

// Overloads virtual function of base class PQTree
// Allows to print debug information on the direction indicators
const char* EmbedPQTree::clientPrintStatus(PQNode<edge, IndInfo*, bool>* nodePtr) {
	if (nodePtr->status() == PQNodeRoot::PQNodeStatus::Indicator) {
		return "Indicator";
	} else {
		return PQTree<edge, IndInfo*, bool>::clientPrintStatus(nodePtr);
	}
}

// The function front scans the frontier of nodePtr. It returns the keys
// of the leaves found in the frontier of nodePtr in a SListPure.
// These keys include keys of direction indicators detected in the frontier.
//
// CAREFUL: Funktion marks all full nodes for destruction.
//			Only to be used in connection with replaceRoot.
//
void EmbedPQTree::front(PQNode<edge, IndInfo*, bool>* nodePtr,
		SListPure<PQBasicKey<edge, IndInfo*, bool>*>& keys) {
	ArrayBuffer<PQNode<edge, IndInfo*, bool>*> S;
	S.push(nodePtr);

	while (!S.empty()) {
		PQNode<edge, IndInfo*, bool>* checkNode = S.popRet();

		if (checkNode->type() == PQNodeRoot::PQNodeType::Leaf) {
			keys.pushBack((PQBasicKey<edge, IndInfo*, bool>*)checkNode->getKey());
		} else {
			PQNode<edge, IndInfo*, bool>* firstSon = nullptr;
			if (checkNode->type() == PQNodeRoot::PQNodeType::PNode) {
				firstSon = checkNode->referenceChild();
			} else if (checkNode->type() == PQNodeRoot::PQNodeType::QNode) {
				firstSon = checkNode->getEndmost(PQNodeRoot::SibDirection::Right);
				// By this, we make sure that we start on the left side
				// since the left endmost child will be on top of the stack
			}

			OGDF_ASSERT(firstSon != nullptr);
			if (firstSon->status() == PQNodeRoot::PQNodeStatus::Indicator) {
				keys.pushBack((PQBasicKey<edge, IndInfo*, bool>*)firstSon->getNodeInfo());
				m_pertinentNodes->pushBack(firstSon);
				destroyNode(firstSon);
			} else {
				S.push(firstSon);
			}

			PQNode<edge, IndInfo*, bool>* nextSon = firstSon->getNextSib(nullptr);
			PQNode<edge, IndInfo*, bool>* oldSib = firstSon;
			while (nextSon && nextSon != firstSon) {
				if (nextSon->status() == PQNodeRoot::PQNodeStatus::Indicator) {
					// Direction indicators point with their left sibling pointer
					// in the direction of their sequence. If an indicator is scanned
					// from the opposite direction, coming from its right sibling
					// the corresponding sequence must be reversed.
					if (oldSib == nextSon->getSib(PQNodeRoot::SibDirection::Left)) {
						// Direction changed
						nextSon->getNodeInfo()->userStructInfo()->changeDir = true;
					}
					keys.pushBack((PQBasicKey<edge, IndInfo*, bool>*)nextSon->getNodeInfo());
					m_pertinentNodes->pushBack(nextSon);
				} else {
					S.push(nextSon);
				}

				PQNode<edge, IndInfo*, bool>* holdSib = nextSon->getNextSib(oldSib);
				oldSib = nextSon;
				nextSon = holdSib;
			}
		}
	}
}

// The function front scans the frontier of nodePtr. It returns the keys
// of the leaves found in the frontier of nodePtr in a SListPure.
// These keys include keys of direction indicators detected in the frontier.
//
// No direction is assigned to the direction indicators.
//
void EmbedPQTree::getFront(PQNode<edge, IndInfo*, bool>* nodePtr,
		SListPure<PQBasicKey<edge, IndInfo*, bool>*>& keys) {
	ArrayBuffer<PQNode<edge, IndInfo*, bool>*> S;
	S.push(nodePtr);

	while (!S.empty()) {
		PQNode<edge, IndInfo*, bool>* checkNode = S.popRet();

		if (checkNode->type() == PQNodeRoot::PQNodeType::Leaf) {
			keys.pushBack((PQBasicKey<edge, IndInfo*, bool>*)checkNode->getKey());
		} else {
			PQNode<edge, IndInfo*, bool>* firstSon = nullptr;
			if (checkNode->type() == PQNodeRoot::PQNodeType::PNode) {
				firstSon = checkNode->referenceChild();
			} else {
				OGDF_ASSERT(checkNode->type() == PQNodeRoot::PQNodeType::QNode);
				firstSon = checkNode->getEndmost(PQNodeRoot::SibDirection::Right);
				// By this, we make sure that we start on the left side
				// since the left endmost child will be on top of the stack
			}

			if (firstSon->status() == PQNodeRoot::PQNodeStatus::Indicator) {
				keys.pushBack((PQBasicKey<edge, IndInfo*, bool>*)firstSon->getNodeInfo());
			} else {
				S.push(firstSon);
			}

			PQNode<edge, IndInfo*, bool>* nextSon = firstSon->getNextSib(nullptr);
			PQNode<edge, IndInfo*, bool>* oldSib = firstSon;
			while (nextSon && nextSon != firstSon) {
				if (nextSon->status() == PQNodeRoot::PQNodeStatus::Indicator) {
					keys.pushBack((PQBasicKey<edge, IndInfo*, bool>*)nextSon->getNodeInfo());
				} else {
					S.push(nextSon);
				}

				PQNode<edge, IndInfo*, bool>* holdSib = nextSon->getNextSib(oldSib);
				oldSib = nextSon;
				nextSon = holdSib;
			}
		}
	}
}

}
}
