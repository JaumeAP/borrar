/*======================================================================*
    Copyright (c) 2015-2023 DTS, Inc. and its affiliates.

    Redistribution and use in source and binary forms, with or without modification,
    are permitted provided that the following conditions are met:

    1. Redistributions of source code must retain the above copyright notice, this
    list of conditions and the following disclaimer.

    2. Redistributions in binary form must reproduce the above copyright notice,
    this list of conditions and the following disclaimer in the documentation
    and/or other materials provided with the distribution.

    3. Neither the name of the copyright holder nor the names of its contributors
    may be used to endorse or promote products derived from this software without
    specific prior written permission.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
    ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
    WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
    DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
    ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
    (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
    LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
    ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
    (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
    SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *======================================================================*/

#include "renderutils/VirtualSources.h"
#include <assert.h>
#include <iterator>
#include <algorithm>
#include <iostream>
#include <string>
#include <iomanip>
#include <cmath>

namespace RenderUtils
{

    VirtualSourceTree::VirtualSourceTree(std::vector<VirtualSource>::size_type pSpeakerGainCount):
        fLeftVirtualSources(NULL),
        fRightVirtualSources(NULL)
    {
        fRoot.fSpeakerGains.resize(pSpeakerGainCount);
    }


    VirtualSourceTree::~VirtualSourceTree()
    {

        if (fLeftVirtualSources)
        {
            delete fLeftVirtualSources;
            fLeftVirtualSources = NULL;
        }

        /* search the right tree if the search interval intersects with the right tree interval  */

        if (fRightVirtualSources)
        {
            delete fRightVirtualSources;
            fRightVirtualSources = NULL;
        }
    }



    void VirtualSourceTree::build(const std::vector<VirtualSource>::const_iterator &fStart,
                                  const std::vector<VirtualSource>::const_iterator &fEnd)
    {
        std::vector<float> temp((*fStart).fSpeakerGains.size(), 0.0f);

        build(fStart, fEnd, temp);

        // Prefix sums of the leaves, when they come in strictly increasing theta order (the tree's own assumption).
        fLeafThetaIndices.clear();
        fPrefixGains.clear();
        fLeavesContiguous = false;

        std::size_t const count = temp.size();
        bool sorted = true;

        for (std::vector<VirtualSource>::const_iterator i = fStart; i != fEnd; i++)
        {
            if ((*i).fSpeakerGains.size() != count || (!fLeafThetaIndices.empty() && (*i).fThetaIndex <= fLeafThetaIndices.back()))
            {
                sorted = false;
                break;
            }
            fLeafThetaIndices.push_back((*i).fThetaIndex);
        }

        if (!sorted)
        {
            fLeafThetaIndices.clear();
            return;
        }

        fLeavesContiguous = fLeafThetaIndices.back() - fLeafThetaIndices.front() + 1 == static_cast<int>(fLeafThetaIndices.size());

        fPrefixGains.assign((fLeafThetaIndices.size() + 1) * count, 0.0);
        std::size_t k = 0;

        for (std::vector<VirtualSource>::const_iterator i = fStart; i != fEnd; i++, k++)
        {
            for (std::size_t g = 0; g < count; g++)
            {
                fPrefixGains[(k + 1) * count + g] = fPrefixGains[k * count + g] + static_cast<double>((*i).fSpeakerGains[g]);
            }
        }
    }


    void VirtualSourceTree::build(const std::vector<VirtualSource>::const_iterator &fStart,
                                  const std::vector<VirtualSource>::const_iterator &fEnd,
                                  std::vector<float> &pSpeakerGains)
    {

        assert(pSpeakerGains.size() == (*fStart).fSpeakerGains.size());

        std::vector<VirtualSource>::difference_type d = std::distance(fStart, fEnd);

        if (d == 1)
        {
            fRoot.fTheta = (*fStart).fTheta;
            fRoot.fThetaIndex = (*fStart).fThetaIndex;
            fRoot.fSpeakerGains = (*fStart).fSpeakerGains;
            fRoot.fCount = 1;

            std::transform(fRoot.fSpeakerGains.begin(), fRoot.fSpeakerGains.end(), pSpeakerGains.begin(),
                           pSpeakerGains.begin(), std::plus<float>());

        }
        else
        {

            std::vector<VirtualSource>::const_iterator mid = fStart + (std::vector<VirtualSource>::size_type) std::ceil((float) d / 2.0f);

            fRoot.fTheta = (*(mid - 1)).fTheta;
            fRoot.fThetaIndex = (*(mid - 1)).fThetaIndex;

            std::vector<float> tmp(pSpeakerGains.size(), 0.0f);

            fLeftVirtualSources = new VirtualSourceTree((*fStart).fSpeakerGains.size());

            fLeftVirtualSources->build(fStart, mid, tmp);

            std::transform(tmp.begin(), tmp.end(), pSpeakerGains.begin(), pSpeakerGains.begin(), std::plus<float>());

            std::fill(tmp.begin(), tmp.end(), 0.0f);

            fRightVirtualSources = new VirtualSourceTree((*fStart).fSpeakerGains.size());

            fRightVirtualSources->build(mid, fEnd, tmp);

            std::transform(tmp.begin(), tmp.end(), pSpeakerGains.begin(), pSpeakerGains.begin(), std::plus<float>());

            fRoot.fSpeakerGains = pSpeakerGains;

            fRoot.fCount = fRightVirtualSources->fRoot.fCount + fLeftVirtualSources->fRoot.fCount;
        }


    }


    int VirtualSourceTree::averageGainsOverRange(int pQueryLow, int pQueryHigh, int pMin, int pMax, std::vector<float> &pSpeakerGains) const
    {
        // Over the whole tree the walk adds the leaves whose theta index is in [pQueryLow, pQueryHigh]: take their sum
        // from the prefix sums instead, one subtraction per gain. The double sum rounds to within a few ulp of the
        // float sum of the walk, not bit for bit.
        std::size_t const count = fRoot.fSpeakerGains.size();

        if (!fLeafThetaIndices.empty() && pSpeakerGains.size() == count
            && pMin <= fLeafThetaIndices.front() && pMax >= fLeafThetaIndices.back())
        {
            std::size_t first;
            std::size_t last;

            if (fLeavesContiguous)
            {
                // Leaves k = 0 .. n-1 have theta index front + k: the positions by arithmetic, no search.
                long const n = static_cast<long>(fLeafThetaIndices.size());
                long const front = fLeafThetaIndices.front();
                first = static_cast<std::size_t>(std::min(std::max(static_cast<long>(pQueryLow) - front, 0L), n));
                last = static_cast<std::size_t>(std::min(std::max(static_cast<long>(pQueryHigh) - front + 1, 0L), n));
            }
            else
            {
                first = std::lower_bound(fLeafThetaIndices.begin(), fLeafThetaIndices.end(), pQueryLow) - fLeafThetaIndices.begin();
                last = std::upper_bound(fLeafThetaIndices.begin(), fLeafThetaIndices.end(), pQueryHigh) - fLeafThetaIndices.begin();
            }

            if (last <= first)
            {
                return 0;
            }

            double const *high = &fPrefixGains[last * count];
            double const *low = &fPrefixGains[first * count];

            for (std::size_t g = 0; g < count; g++)
            {
                pSpeakerGains[g] += static_cast<float>(high[g] - low[g]);
            }

            return static_cast<int>(last - first);
        }

        /* if node, is the tree interval is within the search interval */
        /* if leaf, is the leaf within the search interval */

        if (( pQueryLow <= pMin && pQueryHigh >= pMax ) ||
                (!fLeftVirtualSources && pQueryLow <= fRoot.fThetaIndex && pQueryHigh >= fRoot.fThetaIndex) )
        {
            std::transform(fRoot.fSpeakerGains.begin(), fRoot.fSpeakerGains.end(), pSpeakerGains.begin(), pSpeakerGains.begin(), std::plus<float>());
            return fRoot.fCount;
        }

        int rslt = 0;

        /* search the left tree if the search interval intersects with the left tree interval */

        if (pQueryLow <= fRoot.fThetaIndex && fLeftVirtualSources)
        {
            rslt += fLeftVirtualSources->averageGainsOverRange(pQueryLow, pQueryHigh, pMin, fRoot.fThetaIndex, pSpeakerGains) ;
        }

        /* search the right tree if the search interval intersects with the right tree interval */

        if (pQueryHigh > fRoot.fThetaIndex && fRightVirtualSources)
        {
            rslt += fRightVirtualSources->averageGainsOverRange(pQueryLow, pQueryHigh, fRoot.fThetaIndex, pMax, pSpeakerGains);
        }

        return rslt;
    }



    std::ostream& operator<<(std::ostream& pStream, const HemisphereVirtualSources& pSources)
    {
        for(std::vector<LongitudeVirtualSources>::const_iterator i = pSources.fLongitudes.begin(); i != pSources.fLongitudes.end(); i++)
        {
            pStream << "Phi: " << (*i).fPhi << "| Index:" << (*i).fPhiIndex << "| Theta Max:" << (*i).fMaxThetaIndex << std::endl;

            pStream << *((*i).fVirtualSources);

            pStream << std::endl;
        }

        return pStream;
    }


    std::ostream& operator<<(std::ostream& os, const VirtualSourceTree& q)
    {
        q.print(os);
        return os;
    }


    void VirtualSourceTree::print(std::ostream& os, int depth) const
    {
        std::string tabs(depth*4, ' ');

        if (!fLeftVirtualSources && !fRightVirtualSources)
        {
            os << tabs << "[" << std::setw(3) << fRoot.fTheta << "/" << fRoot.fThetaIndex << "]";
        }
        else
        {
            os << tabs << "<" << std::setw(3) << fRoot.fTheta << "/" << fRoot.fThetaIndex << ">";
        }

        os << " = ";

        for(std::vector<float>::const_iterator i = fRoot.fSpeakerGains.begin(); i != fRoot.fSpeakerGains.end(); i++)
        {
            os << std::setw(3) << (*i) << " ";
        }

        os << std::endl;

        if (fLeftVirtualSources)
        {
            fLeftVirtualSources->print(os, depth + 1);
        }


        if (fRightVirtualSources)
        {
            fRightVirtualSources->print(os, depth + 1);
        }

    }


    std::vector<LongitudeVirtualSources>::const_iterator HemisphereVirtualSources::end() const
    {
        return fLongitudes.end();
    }

} // RenderUtils
