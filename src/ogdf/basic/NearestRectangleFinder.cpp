/** \file
 * \brief Implements class NearestRectangleFinder
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
#include <ogdf/basic/ArrayBuffer.h>
#include <ogdf/basic/List.h>
#include <ogdf/basic/NearestRectangleFinder.h>
#include <ogdf/basic/basic.h>
#include <ogdf/basic/comparer.h>
#include <ogdf/basic/geometry.h>

#include <limits>
#include <ostream>

namespace ogdf {

//! Represents a pair of a coordinate (x or y) and the index of a rectangle
struct OGDF_EXPORT NearestRectangleFinder::PairCoordId {
	PairCoordId(double coord, int index) {
		m_coord = coord;
		m_index = index;
	}

	PairCoordId() = default;

	friend std::ostream& operator<<(std::ostream& os, const PairCoordId& p) {
		os << "(" << p.m_coord << "," << p.m_index << ")";
		return os;
	}

	double m_coord;
	int m_index;
};

void NearestRectangleFinder::find(const Array<RectRegion>& region, const Array<DPoint>& point,
		Array<List<PairRectDist>>& nearest) {
	const int n = region.size(); // number of rectangles
	const int m = point.size(); // number of points

	List<PairCoordId> listTop; // list of max. y-coord. of rectangles
	List<PairCoordId> listBottom; // list of min. y-coord. of rectangles

	// build lists listTop and listBottom ...
	int i;
	for (i = 0; i < n; ++i) {
		const RectRegion& rect = region[i];
		listTop.pushBack(PairCoordId(rect.m_y + rect.m_height / 2.0, i));
		listBottom.pushBack(PairCoordId(rect.m_y - rect.m_height / 2.0, i));
	}

	// ... and sort them by decreasing coordinates
	GenericComparer<PairCoordId, double> comparer([&](const PairCoordId& x) { return -x.m_coord; });
	listTop.quicksort(comparer);
	listBottom.quicksort(comparer);

	// build array of point indices ...
	Array<int> sortedPoints(m);
	for (i = 0; i < m; ++i) {
		sortedPoints[i] = i;
	}

	// ... and sort them by decreasing y-coordinate
	sortedPoints.quicksort(GenericComparer<int, double>([&](int x) { return -point[x].m_y; }));


	ListPure<int> active; // list of rectangles such that y-coord. of current point is contained y-projection of rectangle

	// We traverse the lists listTop and listBottom from start to end such that
	// the coord. of the current entry in listTop is the first entry below p.y
	// and the coord. of the current entry in listBottom is the first entry
	// equal or below p.y
	ListIterator<PairCoordId> nextTop = listTop.begin();
	ListIterator<PairCoordId> nextBottom = listBottom.begin();


	// position of a rectangle in active
	Array<ListIterator<int>> posInActive(n);
	// list of rectangles visited for current point
	ArrayBuffer<int> visitedRectangles(n);
	// distance of rectangle to current point (if contained in visitedRectangles)
	Array<double> distance(n);

	// the maximal distance we have to explore
	// (if a rectangle lies at distance m_maxAllowedDistance, it can get
	// ambigous if there are rectangles with distance <= maxDistanceVisit)
	double maxDistanceVisit = m_maxAllowedDistance + m_toleranceDistance;

	// we iterate over all points by decreasing y-coordinate
	for (i = 0; i < m; ++i) {
		const int nextPoint = sortedPoints[i];
		const DPoint& p = point[nextPoint];

		// update active list
		while (nextTop.valid() && (*nextTop).m_coord >= p.m_y) {
			int index = (*nextTop).m_index;
			posInActive[index] = active.pushBack(index);
			++nextTop;
		}

		while (nextBottom.valid() && (*nextBottom).m_coord > p.m_y) {
			active.del(posInActive[(*nextBottom).m_index]);
			++nextBottom;
		}

		// the largest minDist value we have to consider
		double minDist = maxDistanceVisit;

		// look for rectangles with minimal distance in active rectangles
		// here the distance ist the distance in x-direction
		for (int j : active) {
			const RectRegion& rect = region[j];
			double left = rect.m_x - rect.m_width / 2.0;
			double right = rect.m_x + rect.m_width / 2.0;

			double xDist = 0.0;
			if (p.m_x < left) {
				xDist = left - p.m_x;
			} else if (right < p.m_x) {
				xDist = p.m_x - right;
			}

			if (xDist < minDist) {
				minDist = xDist;
			}

			visitedRectangles.push(j);
			distance[j] = xDist;
		}

		// starting at p.y, we iterate simultaniously upward and downward.
		// We go upward in listBottom since these rectangles lie completely
		// above p, and downward in listTop since these rectangles lie
		// completely below p
		ListIterator<PairCoordId> itTop = nextTop;
		ListIterator<PairCoordId> itBottom = nextBottom.valid()
				? nextBottom.pred()
				: ListIterator<PairCoordId>(listBottom.rbegin());

		while (itTop.valid() || itBottom.valid()) {
			if (itTop.valid()) {
				if ((*itTop).m_coord < p.m_y - minDist) {
					itTop = ListIterator<PairCoordId>();
				} else {
					// determine distance between *itTop and p
					const RectRegion& rect = region[(*itTop).m_index];
					double left = rect.m_x - rect.m_width / 2.0;
					double right = rect.m_x + rect.m_width / 2.0;

					double xDist = 0.0;
					if (p.m_x < left) {
						xDist = left - p.m_x;
					} else if (right < p.m_x) {
						xDist = p.m_x - right;
					}

					double dist = xDist + (p.m_y - (*itTop).m_coord);
					OGDF_ASSERT(dist > 0);

					if (dist < minDist) {
						minDist = dist;
					}

					// update visited rectangles
					visitedRectangles.push((*itTop).m_index);
					distance[(*itTop).m_index] = dist;

					++itTop;
				}
			}

			if (itBottom.valid()) {
				if ((*itBottom).m_coord > p.m_y + minDist) {
					itBottom = ListIterator<PairCoordId>();
				} else {
					// determine distance between *itBottom and p
					const RectRegion& rect = region[(*itBottom).m_index];
					double left = rect.m_x - rect.m_width / 2.0;
					double right = rect.m_x + rect.m_width / 2.0;

					double xDist = 0.0;
					if (p.m_x < left) {
						xDist = left - p.m_x;
					} else if (right < p.m_x) {
						xDist = p.m_x - right;
					}

					double dist = xDist + ((*itBottom).m_coord - p.m_y);
					OGDF_ASSERT(dist > 0);

					if (dist < minDist) {
						minDist = dist;
					}

					// update visited rectangles
					visitedRectangles.push((*itBottom).m_index);
					distance[(*itBottom).m_index] = dist;

					--itBottom;
				}
			}
		}

		// if the minimum found distance is outside the allowed distance
		// we return an empty list for p
		if (minDist > m_maxAllowedDistance) {
			visitedRectangles.clear();

		} else {
			// otherwise we return all rectangles which are at most minimal
			// distance plus tolerance away
			double max = minDist + m_toleranceDistance;
			while (!visitedRectangles.empty()) {
				int index = visitedRectangles.popRet();
				if (distance[index] <= max) {
					nearest[nextPoint].pushBack(PairRectDist(index, distance[index]));
				}
			}
		}
	}
}

// simple version of find() which is used for correction checking
// this version only computes the nearest rectangle without considering
// maxAllowedDistance and toleranceDistance.
void NearestRectangleFinder::findSimple(const Array<RectRegion>& region, const Array<DPoint>& point,
		Array<List<PairRectDist>>& nearest) {
	const int n = region.size();
	const int m = point.size();

	for (int i = 0; i < m; ++i) {
		const DPoint& p = point[i];
		double minDist = std::numeric_limits<double>::max();
		int minDistIndex = -1;

		for (int j = 0; j < n; ++j) {
			const RectRegion& rect = region[j];

			double left = rect.m_x - rect.m_width / 2.0;
			double right = rect.m_x + rect.m_width / 2.0;

			double xDist = 0.0;
			if (p.m_x < left) {
				xDist = left - p.m_x;
			} else if (right < p.m_x) {
				xDist = p.m_x - right;
			}
			OGDF_ASSERT(xDist >= 0);

			double bottom = rect.m_y - rect.m_height / 2.0;
			double top = rect.m_y + rect.m_height / 2.0;

			double yDist = 0.0;
			if (p.m_y < bottom) {
				yDist = bottom - p.m_y;
			} else if (top < p.m_y) {
				yDist = p.m_y - top;
			}
			OGDF_ASSERT(yDist >= 0);

			double dist = xDist + yDist;

			if (dist < minDist) {
				minDist = dist;
				minDistIndex = j;
			}
		}

#if 0
		const RectRegion &rect = region[minDistIndex];
#endif
		if (minDist <= m_maxAllowedDistance) {
			nearest[i].pushBack(PairRectDist(minDistIndex, minDist));
		}
	}
}

}
