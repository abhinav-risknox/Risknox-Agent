import { combineReducers } from "redux";
import LayoutReducer from "./layouts/reducer";

const rootReducer = combineReducers({
    Layout: LayoutReducer,
});

export default rootReducer;
