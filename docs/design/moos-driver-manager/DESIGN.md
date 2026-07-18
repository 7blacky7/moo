---
name: Moos OS Desktop System
colors:
  surface: '#131313'
  surface-dim: '#131313'
  surface-bright: '#393939'
  surface-container-lowest: '#0e0e0e'
  surface-container-low: '#1b1b1c'
  surface-container: '#202020'
  surface-container-high: '#2a2a2a'
  surface-container-highest: '#353535'
  on-surface: '#e5e2e1'
  on-surface-variant: '#c1c6d7'
  inverse-surface: '#e5e2e1'
  inverse-on-surface: '#303030'
  outline: '#8b90a0'
  outline-variant: '#414755'
  surface-tint: '#adc6ff'
  primary: '#adc6ff'
  on-primary: '#002e69'
  primary-container: '#4b8eff'
  on-primary-container: '#00285c'
  inverse-primary: '#005bc1'
  secondary: '#e8b3ff'
  on-secondary: '#510074'
  secondary-container: '#7508a5'
  on-secondary-container: '#e19fff'
  tertiary: '#53e16f'
  on-tertiary: '#003911'
  tertiary-container: '#00a741'
  on-tertiary-container: '#00320e'
  error: '#ffb4ab'
  on-error: '#690005'
  error-container: '#93000a'
  on-error-container: '#ffdad6'
  primary-fixed: '#d8e2ff'
  primary-fixed-dim: '#adc6ff'
  on-primary-fixed: '#001a41'
  on-primary-fixed-variant: '#004493'
  secondary-fixed: '#f6d9ff'
  secondary-fixed-dim: '#e8b3ff'
  on-secondary-fixed: '#310048'
  on-secondary-fixed-variant: '#7201a2'
  tertiary-fixed: '#72fe88'
  tertiary-fixed-dim: '#53e16f'
  on-tertiary-fixed: '#002107'
  on-tertiary-fixed-variant: '#00531c'
  background: '#131313'
  on-background: '#e5e2e1'
  surface-variant: '#353535'
typography:
  display-lg:
    fontFamily: Inter
    fontSize: 48px
    fontWeight: '700'
    lineHeight: 56px
    letterSpacing: -0.02em
  headline-md:
    fontFamily: Inter
    fontSize: 24px
    fontWeight: '600'
    lineHeight: 32px
    letterSpacing: -0.01em
  body-md:
    fontFamily: Inter
    fontSize: 14px
    fontWeight: '400'
    lineHeight: 20px
  label-sm:
    fontFamily: Geist
    fontSize: 12px
    fontWeight: '500'
    lineHeight: 16px
    letterSpacing: 0.05em
  mono-data:
    fontFamily: Geist
    fontSize: 13px
    fontWeight: '400'
    lineHeight: 18px
rounded:
  sm: 0.25rem
  DEFAULT: 0.5rem
  md: 0.75rem
  lg: 1rem
  xl: 1.5rem
  full: 9999px
spacing:
  grid-margin: 24px
  gutter: 12px
  panel-padding: 20px
  stack-gap: 8px
---

## Brand & Style
The design system for this desktop OS prioritizes transparency, depth, and performance. It revives the spirit of "Aero" through a contemporary lens—replacing heavy gloss with high-refraction blurs and precision-engineered layouts. 

The personality is **High-Tech and Intelligent**, designed for power users who value a workspace that feels light yet structurally sound. The visual style is **Glassmorphism**, characterized by translucent layers, vibrant background blurs, and hairline borders that define objects without creating visual weight. It evokes an emotional response of clarity and technical sophistication.

## Colors
The palette is centered on transparency. The default mode is **Dark**, optimized for high-performance hardware and reduced eye strain. 

- **Primary (Blue):** Used for standard Windows operations and core system actions.
- **Moos Native (Purple):** Reserved for proprietary applications and unique OS features.
- **Active Status (Green):** Indicates hardware efficiency and online connectivity.
- **Linux Subsystem (Orange):** Distinguishes terminal environments and cross-platform compatibility.
- **Glass Surfaces:** Bases are derived from the neutral hex with 40% opacity, allowing background wallpapers to bleed through via high-intensity Gaussian blurs (40px-60px).

## Typography
The system utilizes **Inter** for its exceptional legibility on high-resolution displays. For technical data and system labels, **Geist** is introduced to provide a developer-centric, precise aesthetic. 

Headlines should be set with tight letter spacing to maintain a compact, "engineered" look. Labels for hardware stats and status badges should always use the monospaced Geist font to ensure numerical alignment in live-updating dashboards.

## Layout & Spacing
This design system employs a **Fluid Grid** with fixed margins. The desktop environment relies on a window-based layout where components reflow within glass containers.

- **Desktop Margins:** 24px safety area from the edges of the display.
- **Gutter:** 12px between adjacent glass panels to maintain the sense of "floating" layers.
- **Density:** High information density is preferred. Use tight 8px gaps for vertical stacks (lists) and 20px padding for main window content to ensure elements don't feel crowded against the glass edges.

## Elevation & Depth
Depth is communicated through **Backdrop Blurs** and **Inner Glows** rather than traditional drop shadows. 

1.  **Base Layer:** The desktop wallpaper.
2.  **Standard Windows:** 40px backdrop-filter blur, 1px solid border (rgba 255, 255, 255, 0.1), and a subtle 10% white inner stroke to simulate a light-catching glass edge.
3.  **Active/Focused States:** An external glow using the primary or accent color (low spread, high intensity) signals the active window.
4.  **Floating Modals:** Increased blur (80px) and a darker tint to the glass surface to pull them forward in the Z-space.

## Shapes
The shape language is "Soft-Tech." All glass panels and windows use a **16px (rounded-lg)** corner radius to feel approachable and modern. Smaller internal components like buttons and input fields use an **8px (standard)** radius. This nesting of curves creates a hierarchical sense of containment.

## Components
- **Glass Panels:** The fundamental container. Must include a `backdrop-filter: blur(40px)` and a `linear-gradient` border from top-left to bottom-right to simulate light.
- **Glowing Buttons:** Primary buttons feature a subtle outer glow of their own color. Hover states should increase brightness and "pulse" the glow slightly.
- **Status Badges:** Compact pills with a solid background of the status color (Green, Purple, etc.) and high-contrast text. Use for CPU load, OS type, and connectivity.
- **Hardware Lists:** Organized in rows with 1px glass separators. Use monospaced fonts for numerical values (e.g., RAM usage, clock speed) to ensure data stability during updates.
- **Input Fields:** Recessed glass effect using a darker background tint and an inner shadow to suggest an "etched" surface in the panel.
- **System Tray:** A persistent glass bar at the edge of the screen with condensed spacing and icon-centric labels.