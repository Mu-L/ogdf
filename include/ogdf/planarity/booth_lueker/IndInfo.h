/** \file
 * \brief Declaration of the class EmbedKey.
 *
 * Implements the Key of the direction Indicator. Used by class EmbedPQTree.
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

#pragma once

#include <ogdf/basic/Graph.h>
#include <ogdf/basic/memory.h>

namespace ogdf {
namespace booth_lueker {

class IndInfo {
	friend class EmbedPQTree;

public:
	explicit IndInfo(node w) {
		v = w;
		changeDir = false;
	}

	~IndInfo() { }

	void resetAssociatedNode(node w) { v = w; }

	node getAssociatedNode() { return v; }

private:
	node v;
	bool changeDir;


	OGDF_NEW_DELETE
};

}
}
