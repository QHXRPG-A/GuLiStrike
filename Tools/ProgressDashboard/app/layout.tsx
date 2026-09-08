import type { Metadata } from 'next';
import './globals.css';

export const metadata: Metadata = {
  title: 'GuLiStrike · Progress Control',
  description: 'GuLiStrike 本地只读进度与归档战情面板',
};

export default function RootLayout({
  children,
}: Readonly<{
  children: React.ReactNode;
}>) {
  return (
    <html lang="zh-CN" className="dark">
      <body>{children}</body>
    </html>
  );
}
