module.exports = {
    darkMode: 'class',
    content: [
        "./index.html",
        "./src/**/*.{vue,js,ts,jsx,tsx}",
    ],
    theme: {
        extend: {
            colors: {
                primary: {
                    50: '#f5f3ff',
                    100: '#ede9fe',
                    500: '#6366f1',
                    600: '#4f46e5',
                    700: '#4338ca',
                }
            },
            animation: {
                'bounce-slow': 'bounce 3s linear infinite',
            }
        },
    },
    plugins: [
        require('@tailwindcss/typography'),
        require('daisyui')
    ],
    daisyui: {
        themes: ["light", "dark"],
        darkTheme: "dark",
    }
} 