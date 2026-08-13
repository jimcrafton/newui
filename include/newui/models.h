#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "newui/newui.h"
#include "newui/delegate.h"


namespace newui {
    class View;

    class Model {
        enum UpdateFlags {
            NoFlags = 0x00,
            RequiresValidation = 0x001,
            DisplayErrorIfInvalid = 0x002,            
        };

        public:
            typedef Delegate<Model> ModelDelegate;
            typedef Delegate<Model,const std::any&, const std::any&> ModelKeyValueDelegate;
            
            //returns bool, takes key, value, and outResult
            //using ValidatorFuncPtr = bool(*)(const std::any&, const std::any&, std::any& );

            ModelDelegate onChanged;
            ModelDelegate onCleared;
            ModelKeyValueDelegate onKeyValueChanged;

            virtual void clear() {                
                onCleared(*this);
            }

            virtual bool empty() const {
                return true;
            }

            virtual std::any value( const std::any& key=std::any() ) {
                return std::any();
            }

            virtual void setValue( const std::any& newValue, const std::any& key=std::any());

            void addView(View*);
            void removeView(View*);
            void updateAllViews();
        private:
            //views are not owned by model
            //when a view is added, the model adds itself to the views
            //onDestroyed delegate, if it's triggered, it removes the view.
            std::vector<View*> views_;
            uint32_t updateFlags_ = UpdateFlags::NoFlags;


    }
}