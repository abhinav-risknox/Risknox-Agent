import React, { useEffect } from 'react';
import withRouter from '../components/common/withRouter';

//redux
import { useSelector } from "react-redux";
import { createSelector } from 'reselect';

const NonAuthLayout = ({ children }) => {

    const selectLayoutState = (state) => state.Layout;
    const selectLayoutProperties = createSelector(
        selectLayoutState,
        (state) => ({
            layoutModeType: state.layoutModeType,
            backgroundImageType: state.backgroundImageType,
        })
    );
    // Inside your component
    const {
        layoutModeType, backgroundImageType
    } = useSelector(selectLayoutProperties);

    useEffect(() => {
        const landing = window.location.pathname.slice(1);
        const nftLanding = window.location.pathname.slice(1);

        if (layoutModeType === "dark") {
            document.body.setAttribute("data-bs-theme", "dark");
            document.documentElement.setAttribute("data-bs-theme", "dark");
            document.documentElement.setAttribute("data-body-image", backgroundImageType);

            if (landing === "landing") { document.documentElement.setAttribute("data-body-image", "none"); }
            if (nftLanding === "nft-landing") { document.documentElement.setAttribute("data-body-image", "none"); }

        } else {
            document.body.setAttribute("data-bs-theme", "light");
            document.documentElement.setAttribute("data-bs-theme", "light");
            document.documentElement.setAttribute("data-body-image", backgroundImageType);

            if (landing === "landing") { document.documentElement.setAttribute("data-body-image", "none"); }
            if (nftLanding === "nft-landing") { document.documentElement.setAttribute("data-body-image", "none"); }
        }
        return () => {
            document.body.removeAttribute("data-bs-theme");
            document.documentElement.removeAttribute("data-bs-theme");
            document.documentElement.removeAttribute("data-body-image");
        }
    }, [layoutModeType, backgroundImageType]);
    return (
        <div>
            {children}
        </div>
    );
};

export default withRouter(NonAuthLayout);
