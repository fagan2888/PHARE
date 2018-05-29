#ifndef PHARE_SRC_AMR_FIELD_FIELD_GEOMETRY_H
#define PHARE_SRC_AMR_FIELD_FIELD_GEOMETRY_H

#include <SAMRAI/hier/BoxGeometry.h>

#include "data/grid/gridlayoutdefs.h"
#include "field_overlap.h"
#include "utilities/types.h"

namespace PHARE
{
template<std::size_t dim, typename GridLayout, typename PhysicalQuantity>
class FieldGeometry : public SAMRAI::hier::BoxGeometry
{
public:
    /** \brief Construct a FieldGeometry on a region, for a specific quantity,
     * with a temporary gridlayout
     */
    FieldGeometry(SAMRAI::hier::Box const& box, GridLayout layout, PhysicalQuantity qty)
        : box_{box}
        , layout_{std::move(layout)}
        , quantity_{qty}
    {
    }




    /** \brief calculate overlap from two geometry boxes
     * When we want to calculate an overlap from two FieldGeometry, we give two boxes:
     * sourceMask : which is a region of the source patch and fillBox which is a region
     * on the destination patch, then we tell if we want to overwrite the interior of
     * the destination, after that we have to tell how the sourceMask should be transformed
     * to intersect it with the destination box (for example: when using periodics boundary,
     * the mask will be shifted).
     * Finaly we can also add some restrictions on the boxes for the destination.
     */
    std::shared_ptr<SAMRAI::hier::BoxOverlap>
    calculateOverlap(SAMRAI::hier::BoxGeometry const& destinationGeometry,
                     SAMRAI::hier::BoxGeometry const& sourceGeometry,
                     SAMRAI::hier::Box const& sourceMask, SAMRAI::hier::Box const& fillBox,
                     bool const overwriteInterior, SAMRAI::hier::Transformation const& sourceOffset,
                     bool const retry,
                     SAMRAI::hier::BoxContainer const& destinationRestrictBoxes
                     = SAMRAI::hier::BoxContainer{}) const final
    {
        std::shared_ptr<SAMRAI::hier::BoxOverlap> overlap;
        auto destinationCast = dynamic_cast<FieldGeometry const*>(&destinationGeometry);
        auto sourceCast      = dynamic_cast<FieldGeometry const*>(&sourceGeometry);

        if ((destinationCast != 0) && (sourceCast != 0))
        {
            overlap = doOverlap_(*destinationCast, *sourceCast, sourceMask, fillBox,
                                 overwriteInterior, sourceOffset, destinationRestrictBoxes);
        }
        else if (retry)
        {
            overlap = sourceGeometry.calculateOverlap(
                destinationGeometry, sourceGeometry, sourceMask, fillBox, overwriteInterior,
                sourceOffset, false, destinationRestrictBoxes);
        }
        return overlap;
    }




    std::shared_ptr<SAMRAI::hier::BoxOverlap>
    setUpOverlap(SAMRAI::hier::BoxContainer const& boxes,
                 SAMRAI::hier::Transformation const& offset) const final
    {
        SAMRAI::hier::BoxContainer destinationBox;

        for (auto& box : boxes)
        {
            GridLayout layout = layoutFromBox_(box, layout_);
            SAMRAI::hier::Box fieldBox(toFieldBox(box, quantity_, layout, false));
            destinationBox.push_back(fieldBox);
        }

        return std::make_shared<FieldOverlap<dim>>(destinationBox, offset);
    }




    /**
     * @brief toFieldBox takes an AMR cell-centered box and creates a box
     * that is adequate for the specified quantity. The layout is used to know
     * the centering, nbr of ghosts of the specified quantity.
     * @withGhost true if we want to include the ghost nodes in the field box.
     */
    static SAMRAI::hier::Box toFieldBox(SAMRAI::hier::Box box, PhysicalQuantity qty,
                                        GridLayout const& layout, bool withGhost = true)
    {
        SAMRAI::hier::IntVector lower = box.lower();
        constexpr std::uint32_t dirX{0};
        constexpr std::uint32_t dirY{1};
        constexpr std::uint32_t dirZ{2};

        // lower/upper of 'box' are AMR cell-centered coordinates

        // we want a box that starts at lower
        // the upper index is

        // example:
        // . = primal node
        // x = dual node
        // here we have nbrGhost = 1 for both

        //     0     1     2     3     4     5     6        (local dual Index)
        //  0     1     2     3     4     5     6     7     (local primal Index)
        //  .  x  |  x  .  x  .  x  .  x  .  x  |  x  .
        //           ^
        //         box.lower (AMR index)

        // for primal = AMR upper index will be box.lower + 6 (pei) -1(psi) = 5 without ghosts
        // for dual = AMR upper index will be  box.lower + 5(pei) - 1(psi) = lower+ 4 without ghosts

        // with ghosts :
        // box.lower must be shifted left to move to the first ghost node
        // box.upper is still box.lower + end-start, end &start of ghosts


        if (!withGhost)
        {
            uint32 xStart = layout.physicalStartIndex(qty, Direction::X);
            uint32 xEnd   = layout.physicalEndIndex(qty, Direction::X);

            box.setLower(dirX, lower[dirX]);
            box.setUpper(dirX, xEnd - xStart + lower[dirX]);

            if (dim > 1)
            {
                uint32 yStart = layout.physicalStartIndex(qty, Direction::Y);
                uint32 yEnd   = layout.physicalEndIndex(qty, Direction::Y);

                box.setLower(dirY, lower[dirY]);
                box.setUpper(dirY, yEnd - yStart + lower[dirY]);
            }
            if (dim > 2)
            {
                uint32 zStart = layout.physicalStartIndex(qty, Direction::Z);
                uint32 zEnd   = layout.physicalEndIndex(qty, Direction::Z);

                box.setLower(dirZ, lower[dirZ]);
                box.setUpper(dirZ, zEnd - zStart + lower[dirZ]);
            }
        } // end withGhosts



        else
        {
            auto const& centering = GridLayout::centering(qty);

            SAMRAI::hier::IntVector shift(box.getDim());
            shift[dirX] = layout.nbrGhostNodes(centering[dirX]);

            if (dim > 1)
            {
                shift[dirY] = layout.nbrGhostNodes(centering[dirY]);
            }
            if (dim > 2)
            {
                shift[dirZ] = layout.nbrGhostNodes(centering[dirZ]);
            }

            lower = lower - shift;

            uint32 xStart = layout.ghostStartIndex(qty, Direction::X);
            uint32 xEnd   = layout.ghostEndIndex(qty, Direction::X);

            box.setLower(dirX, lower[dirX]);
            box.setUpper(dirX, xEnd - xStart + lower[dirX]);

            if (dim > 1)
            {
                uint32 yStart = layout.ghostStartIndex(qty, Direction::Y);
                uint32 yEnd   = layout.ghostEndIndex(qty, Direction::Y);

                box.setLower(dirY, lower[dirY]);
                box.setUpper(dirY, yEnd - yStart + lower[dirY]);
            }
            if (dim > 2)
            {
                uint32 zStart = layout.ghostStartIndex(qty, Direction::Z);
                uint32 zEnd   = layout.ghostEndIndex(qty, Direction::Z);

                box.setLower(dirZ, lower[dirZ]);
                box.setUpper(dirZ, zEnd - zStart + lower[dirZ]);
            }
        }

        return box;
    }



private:
    SAMRAI::hier::Box box_;
    GridLayout layout_;
    PhysicalQuantity quantity_;



    void computeDestinationBoxes_(SAMRAI::hier::BoxContainer& destinationBoxes,
                                  FieldGeometry const& sourceGeometry,
                                  SAMRAI::hier::Box const& sourceMask,
                                  SAMRAI::hier::Box const& fillBox, bool const overwriteInterior,
                                  SAMRAI::hier::Transformation const& sourceOffset,
                                  SAMRAI::hier::BoxContainer const& destinationRestrictBoxes
                                  = SAMRAI::hier::BoxContainer()) const
    {
        // we have three boxes :
        // - the sourceBox : the is where the data is to be taken from
        // - the destinationBox : this is the whole box on the "destination"
        // - the fillBox: this is a restriction of the destinationBox

        // all these boxes are in AMR (cell-centered) indexes.
        // we need to translate them into boxes describing the proper field layout.
        // (for instance, a cell-centered box is not adequatly describing a node centered
        // quantity).

        // the destinationsBoxes is to be filled with:
        // the intersection of the sourceBox, the destinationBox and the fillBox
        // with potential restrictionBoxes.
        // In case we don't want to fill interior nodes, we remove the interior
        // of the destination box, from the above intersection, which may result
        // in adding multiple boxes into the container.


        // the sourceMask is a restriction of the sourceBox
        // so we need to intersect it with the sourceBox, then to apply a transformation
        // to account for the periodicity
        SAMRAI::hier::Box sourceShift = sourceGeometry.box_ * sourceMask;
        sourceOffset.transform(sourceShift);


        // ok let's get the boxes for the fields from cell-centered boxes now
        bool withGhosts = true;

        GridLayout sourceShiftLayout = layoutFromBox_(sourceShift, sourceGeometry.layout_);
        GridLayout fillBoxLayout     = layoutFromBox_(fillBox, layout_);

        SAMRAI::hier::Box const destinationField{toFieldBox(box_, quantity_, layout_)};
        SAMRAI::hier::Box const sourceField{
            toFieldBox(sourceShift, quantity_, sourceShiftLayout, !withGhosts)};
        SAMRAI::hier::Box const fillField{toFieldBox(fillBox, quantity_, fillBoxLayout)};


        // now we have all boxes shifted and translated to field boxes
        // let's compute the interesection of them all.

        SAMRAI::hier::Box const together(destinationField * sourceField * fillField);


        // if the interesection is not empty we either push it into the container
        // if we don't want to fill the interior we remove it from the intersection
        // which may add multiple boxes to the container.
        if (!together.empty())
        {
            if (overwriteInterior)
            {
                destinationBoxes.push_back(together);
            }
            else
            {
                SAMRAI::hier::Box interiorBox(toFieldBox(box_, quantity_, layout_, !withGhosts));
                destinationBoxes.removeIntersections(together, interiorBox);
            }
        }

        if (!destinationRestrictBoxes.empty() && !destinationBoxes.empty())
        {
            SAMRAI::hier::BoxContainer restrictBoxes;
            for (auto box = destinationRestrictBoxes.begin(); box != destinationRestrictBoxes.end();
                 ++box)
            {
                restrictBoxes.push_back(toFieldBox(*box, quantity_, layoutFromBox_(*box, layout_)));
            }

            // will only keep of together the boxes that interesect the restrictions
            destinationBoxes.intersectBoxes(restrictBoxes);
        }
    }



    /**
     * @brief doOverlap_ will return a field overlap from the source to the dest
     * geometry. This function makes this overlap by calculating the (possibly multiple)
     * destination box(es) and the transformation from source to dest.
     */
    std::shared_ptr<SAMRAI::hier::BoxOverlap>
    doOverlap_(FieldGeometry const& destinationGeometry, FieldGeometry const& sourceGeometry,
               SAMRAI::hier::Box const& sourceMask, SAMRAI::hier::Box const& fillBox,
               bool const overwriteInterior, SAMRAI::hier::Transformation const& sourceOffset,
               SAMRAI::hier::BoxContainer const& destinationRestrictBoxes) const
    {
        SAMRAI::hier::BoxContainer destinationBox;

        destinationGeometry.computeDestinationBoxes_(destinationBox, sourceGeometry, sourceMask,
                                                     fillBox, overwriteInterior, sourceOffset,
                                                     destinationRestrictBoxes);

        return std::make_shared<FieldOverlap<dim>>(destinationBox, sourceOffset);
    }



    GridLayout layoutFromBox_(SAMRAI::hier::Box const& box, GridLayout const& layout) const
    {
        std::array<uint32, dim> nbCell;
        for (std::size_t iDim = 0; iDim < dim; ++iDim)
        {
            nbCell[iDim] = static_cast<uint32>(box.numberCells(iDim));
        }

        return GridLayout(layout.dxdydz(), nbCell, layout.layoutName(), layout.origin(),
                          layout.order());
    }
};




} // namespace PHARE

#endif
