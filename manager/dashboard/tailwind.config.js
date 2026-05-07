/** @type {import('tailwindcss').Config} */
export default {
  content: [
    "./index.html",
    "./src/**/*.{js,ts,jsx,tsx}",
  ],
  theme: {
    extend: {
      colors: {
        rn: {
          orange: '#FF5B00',
          black: '#1E1E1F',
          white: '#FFFFFF',
          platinum: '#EAEAEB',
          lime: '#FFFD98',
          'orange-dim': '#CC4900',
          'black-soft': '#2A2A2B',
          'black-card': '#242425',
        },
        border: "hsl(var(--border))",
        input: "hsl(var(--input))",
        ring: "hsl(var(--ring))",
        background: "hsl(var(--background))",
        foreground: "hsl(var(--foreground))",
        primary: {
          DEFAULT: "#FF5B00",
          foreground: "#FFFFFF",
        },
        secondary: {
          DEFAULT: "#EAEAEB",
          foreground: "#1E1E1F",
        },
        destructive: {
          DEFAULT: "hsl(var(--destructive))",
          foreground: "hsl(var(--destructive-foreground))",
        },
        muted: {
          DEFAULT: "hsl(var(--muted))",
          foreground: "hsl(var(--muted-foreground))",
        },
        accent: {
          DEFAULT: "#FFFD98",
          foreground: "#1E1E1F",
        },
        popover: {
          DEFAULT: "hsl(var(--popover))",
          foreground: "hsl(var(--popover-foreground))",
        },
        card: {
          DEFAULT: "#242425",
          foreground: "#FFFFFF",
        },
      },
      fontFamily: {
        sans: ['Poppins', 'sans-serif'],
        display: ['DM Sans', 'sans-serif'],
      },
      borderRadius: {
        lg: "var(--radius)",
        md: "calc(var(--radius) - 2px)",
        sm: "calc(var(--radius) - 4px)",
      },
    },
  },
  plugins: [],
}
